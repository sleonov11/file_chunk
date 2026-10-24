#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <optional>

struct FileState {
    std::vector<uint8_t> buffer;
    std::vector<bool> filled;
    size_t fileSize = 0;
    size_t filledCount = 0;
    bool completed = false;
};

class FileCollector {
public:
    void CollectFile(uint32_t fileId, size_t fileSize);
    void OnNewChunk (uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk);
    std::optional<std::vector<uint8_t>> GetFile (uint32_t fileId) const;
    private:
    std::unordered_map<uint32_t, FileState> files;
};