#include "FileCollector.hpp"

#include <cstring>
#include <algorithm>

void FileCollector::CollectFile(uint32_t fileId, size_t fileSize) {
    FileState& fs = files[fileId];
    fs.fileSize = fileSize;
    fs.buffer.assign(fileSize, 0);
    fs.filled.assign(fileSize, false);
    fs.filledCount = 0;
    fs.completed = (fileSize == 0);
}

void FileCollector::OneNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk) {
    auto it = files.find(fileId);
    if (it == files.end()) return;

    FileState& fs = it->second;

    if (fs.completed) return;

    if (chunk.size() == 0) return;
    if (pos >= fs.fileSize) return;

    size_t len = std::min(chunk.size(), fs.fileSize - pos);
    
    
    memcpy(fs.buffer.data() + pos, chunk.data(), len);

    size_t endPos = len + pos;
    for (size_t i = pos; i < endPos; ++i) {
        uint8_t wasNotFilled = !fs.filled[i];
        fs.filledCount += wasNotFilled;
        fs.filled[i] = true;
    }

    fs.completed = fs.filledCount == fs.fileSize;
}