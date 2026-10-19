// SPDX-License-Identifier: MIT
#include <interi/editor/MaterialPreview.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace interi::editor
{

namespace
{

constexpr float kPi = std::numbers::pi_v<float>;

Vec3 Lerp3(const Vec3& a, const Vec3& b, float t)
{
    return a + (b - a) * t;
}

std::uint8_t EncodeSrgb(float linear)
{
    const float c = std::clamp(linear, 0.0f, 1.0f);
    const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(std::clamp(s, 0.0f, 1.0f) * 255.0f));
}

} // namespace

assets::Image8 RenderMaterialPreview(const assets::PbrMaterial& material, std::uint32_t size,
                                     const TextureSampler& sampler, const MaterialPreviewSettings& settings)
{
    assets::Image8 image;
    image.width = size;
    image.height = size;
    image.texels.assign(std::size_t{size} * size * 4, 0);
    if (size == 0)
    {
        return image;
    }
    const Vec3 light = NormalizeOrZero(settings.toLight);
    const Vec3 view(0.0f, 0.0f, 1.0f);
    const auto sample = [&](const assets::TextureRef& ref, float u, float v)
    { return ref.IsSet() && sampler ? sampler(ref, u, v) : Vec4(1.0f, 1.0f, 1.0f, 1.0f); };
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            // The sphere fills the image: pixel centre to [-1, 1] with +y up.
            const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
            const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f;
            const float r2 = px * px + py * py;
            if (r2 > 1.0f)
            {
                continue;
            }
            const Vec3 n(px, py, std::sqrt(1.0f - r2));
            const float u = std::atan2(n.x, n.z) / (2.0f * kPi) + 0.5f;
            const float v = std::acos(std::clamp(n.y, -1.0f, 1.0f)) / kPi;

            Vec4 base = sample(material.baseColorTexture, u, v);
            const Vec3 albedo(material.baseColor.x * base.x, material.baseColor.y * base.y,
                              material.baseColor.z * base.z);
            const float alpha = std::clamp(material.baseColor.w * base.w, 0.0f, 1.0f);
            const Vec4 mr = sample(material.metallicRoughnessTexture, u, v);
            const float metallic = std::clamp(material.metallic * mr.z, 0.0f, 1.0f);
            const float roughness = std::clamp(material.roughness * mr.y, 0.045f, 1.0f);
            const float occlusion =
                1.0f + material.occlusionStrength * (sample(material.occlusionTexture, u, v).x - 1.0f);
            const Vec4 emissiveTexel = sample(material.emissiveTexture, u, v);
            const Vec3 emissive(material.emissive.x * emissiveTexel.x, material.emissive.y * emissiveTexel.y,
                                material.emissive.z * emissiveTexel.z);

            const Vec3 f0 = Lerp3(Vec3(0.04f), albedo, metallic);
            const Vec3 diffuse = albedo * (1.0f - metallic);
            const float nDotL = std::max(Dot(n, light), 0.0f);
            const float nDotV = std::max(Dot(n, view), 1e-4f);
            const Vec3 half = NormalizeOrZero(light + view);
            const float nDotH = std::max(Dot(n, half), 0.0f);
            const float vDotH = std::max(Dot(view, half), 0.0f);
            const float a = roughness * roughness;
            const float a2 = a * a;
            const float denominator = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
            const float distribution = a2 / (kPi * denominator * denominator);
            const float k = a / 2.0f;
            const float visibility = (nDotL / (nDotL * (1.0f - k) + k + 1e-6f)) * (nDotV / (nDotV * (1.0f - k) + k)) /
                                     std::max(4.0f * nDotL * nDotV, 1e-6f);
            const float fresnelWeight = std::pow(1.0f - vDotH, 5.0f);
            const Vec3 fresnel = f0 + (Vec3(1.0f) - f0) * fresnelWeight;
            const Vec3 specular = fresnel * (distribution * visibility);

            const Vec3 sky(0.30f, 0.38f, 0.52f);
            const Vec3 ground(0.07f, 0.065f, 0.06f);
            const Vec3 ambient = Lerp3(ground, sky, n.y * 0.5f + 0.5f);
            const Vec3 environmentSpecular = f0 * ((1.0f - roughness) * 0.6f + 0.1f);
            Vec3 color = (diffuse * (1.0f / kPi) + specular) * settings.lightColor * nDotL +
                         (diffuse + environmentSpecular) * ambient * occlusion + emissive;
            color = Vec3(color.x / (1.0f + color.x), color.y / (1.0f + color.y), color.z / (1.0f + color.z));

            std::uint8_t* out = &image.texels[(std::size_t{y} * size + x) * 4];
            out[0] = EncodeSrgb(color.x);
            out[1] = EncodeSrgb(color.y);
            out[2] = EncodeSrgb(color.z);
            out[3] = material.alphaMode == assets::AlphaMode::Blend
                         ? static_cast<std::uint8_t>(std::lround(alpha * 255.0f))
                         : std::uint8_t{255};
        }
    }
    return image;
}

} // namespace interi::editor
