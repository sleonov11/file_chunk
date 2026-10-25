#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <memory>

struct FileState {
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> filled;
    size_t fileSize = 0;
    size_t filledCount = 0;
    bool completed = false;
    mutable std::mutex mtx;
    std::shared_ptr<const std::vector<uint8_t>> cached;
};

class FileCollector {
public:
    void CollectFile(uint32_t fileId, size_t fileSize);
    void OnNewChunk (uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk);
    std::shared_ptr<const std::vector<uint8_t>> GetFile (uint32_t fileId) const;

private:
    std::unordered_map<uint32_t, FileState> files;
    mutable std::shared_mutex smtx;
};