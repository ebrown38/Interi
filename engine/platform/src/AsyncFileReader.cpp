// SPDX-License-Identifier: MIT
#include <interi/platform/AsyncFileReader.h>

#include <utility>

namespace interi
{

AsyncFileReader::AsyncFileReader(JobSystem& jobs, const IFileSystem& fileSystem)
    : m_jobs(jobs)
    , m_fileSystem(fileSystem)
{
}

AsyncFileReader::~AsyncFileReader()
{
    WaitAll();
}

AsyncFileReader::RequestId AsyncFileReader::Request(std::string path)
{
    auto request = std::make_unique<PendingRead>();
    request->path = std::move(path);
    PendingRead* pending = request.get();
    RequestId id = 0;
    {
        const std::scoped_lock lock(m_mutex);
        id = m_nextId++;
        m_requests.emplace(id, std::move(request));
        ++m_pending;
    }
    m_jobs.Submit([this, pending] { Run(*pending); }, &m_inFlight, JobPriority::Low);
    return id;
}

void AsyncFileReader::Run(PendingRead& request)
{
    ReadResult result = m_fileSystem.ReadFile(request.path);
    const std::scoped_lock lock(m_mutex);
    request.result = std::move(result);
    request.done = true;
    --m_pending;
}

bool AsyncFileReader::IsDone(RequestId id) const
{
    const std::scoped_lock lock(m_mutex);
    const auto found = m_requests.find(id);
    return found != m_requests.end() && found->second->done;
}

std::optional<AsyncFileReader::ReadResult> AsyncFileReader::TryTake(RequestId id)
{
    std::unique_ptr<PendingRead> request;
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_requests.find(id);
        if (found == m_requests.end() || !found->second->done)
        {
            return std::nullopt;
        }
        request = std::move(found->second);
        m_requests.erase(found);
    }
    return std::move(request->result);
}

void AsyncFileReader::WaitAll()
{
    m_jobs.WaitFor(m_inFlight);
}

std::size_t AsyncFileReader::PendingCount() const
{
    const std::scoped_lock lock(m_mutex);
    return m_pending;
}

} // namespace interi
