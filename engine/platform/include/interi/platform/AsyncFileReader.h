// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/jobs/JobSystem.h>
#include <interi/platform/FileSystem.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace interi
{

/// Reads whole files in the background on Low-priority jobs (IO never delays High/Normal work).
///
/// Completion is observed by polling (IsDone/TryTake) or by WaitAll. Never wait for a read from inside a job:
/// while the JobSystem has workers, WaitFor does not run Low-priority jobs, so a job that waits on IO can end up
/// waiting for work that nobody picks up. Request/IsDone/TryTake are thread-safe; WaitAll and the destructor must
/// run on a thread that is not executing a job. The destructor waits for every request still in flight.
class AsyncFileReader
{
public:
    using RequestId = std::uint64_t;
    using ReadResult = Result<std::vector<std::byte>>;

    /// Both must outlive the reader. `fileSystem` must allow concurrent reads.
    AsyncFileReader(JobSystem& jobs, const IFileSystem& fileSystem);
    ~AsyncFileReader();

    AsyncFileReader(const AsyncFileReader&) = delete;
    AsyncFileReader& operator=(const AsyncFileReader&) = delete;
    AsyncFileReader(AsyncFileReader&&) = delete;
    AsyncFileReader& operator=(AsyncFileReader&&) = delete;

    /// Starts reading `path`. Returns an id that is never reused by this reader.
    [[nodiscard]] RequestId Request(std::string path);

    /// True once the read has finished and its result hasn't been taken yet.
    [[nodiscard]] bool IsDone(RequestId id) const;

    /// The result of a finished read, removing it from the reader. Nothing if the read is still running, was
    /// already taken, or the id is unknown.
    [[nodiscard]] std::optional<ReadResult> TryTake(RequestId id);

    /// Blocks until every request made so far has finished. The calling thread helps when there are no workers.
    void WaitAll();

    /// Reads still running.
    [[nodiscard]] std::size_t PendingCount() const;

private:
    struct PendingRead
    {
        std::string path;
        std::optional<ReadResult> result; ///< Written by the job; read only after `done` is observed.
        bool done = false;                ///< Guarded by m_mutex.
    };

    void Run(PendingRead& request);

    JobSystem& m_jobs;
    const IFileSystem& m_fileSystem;
    JobCounter m_inFlight;
    mutable std::mutex m_mutex;
    std::unordered_map<RequestId, std::unique_ptr<PendingRead>> m_requests;
    RequestId m_nextId = 1;
    std::size_t m_pending = 0;
};

} // namespace interi
