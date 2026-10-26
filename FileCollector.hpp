#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <optional>

struct FileState {
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> filled; // TODO: change to vector<uint64_t> and hand adress arithmetic;
    size_t fileSize = 0;
    size_t filledCount = 0;
    mutable std::mutex mtx;
    std::shared_ptr<const std::vector<uint8_t>> cached;
    bool completed = false;
};

class FileCollector {
public:
    void CollectFile(uint32_t fileId, size_t fileSize);
    void OnNewChunk (uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk);
    std::shared_ptr<const std::vector<uint8_t>> GetFile (uint32_t fileId) const;
    std::optional<std::vector<uint8_t>> GetPartFile (uint32_t fileId, size_t offset, size_t size) const;

private:
    std::unordered_map<uint32_t, FileState> files;
    mutable std::shared_mutex smtx;
};