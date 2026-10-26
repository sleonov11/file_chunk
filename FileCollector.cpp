#include "FileCollector.hpp"

#include <cstring>
#include <algorithm>

void FileCollector::CollectFile(uint32_t fileId, size_t fileSize) {
    FileState* fsp;
    {
    std::unique_lock<std::shared_mutex> slock(smtx);
    fsp = &files[fileId];
    }
    std::lock_guard<std::mutex> lock(fsp->mtx);
    fsp->fileSize = fileSize;
    fsp->buffer.assign(fileSize, 0);
    fsp->filled.assign(fileSize, false);
    fsp->filledCount = 0;
    fsp->cached.reset();
    fsp->completed = (fileSize == 0);
    if (fsp->completed) fsp->cached = std::make_shared<const std::vector<uint8_t>>();
}

void FileCollector::OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk) {
    FileState* fsp;
    {
    std::shared_lock<std::shared_mutex> slock(smtx);
    auto it = files.find(fileId);
    
    if (it == files.end()) return;
    
    fsp = &it->second;
    }
    std::lock_guard<std::mutex> lock(fsp->mtx);
    if (fsp->completed) return;

    if (chunk.size() == 0) return;
    if (pos >= fsp->fileSize) return;

    size_t len = std::min(chunk.size(), fsp->fileSize - pos);
    
    std::memcpy(fsp->buffer.data() + pos, chunk.data(), len);

    size_t endPos = len + pos;
    for (size_t i = pos; i < endPos; ++i) {
        uint8_t wasNotFilled = !fsp->filled[i];
        fsp->filledCount += wasNotFilled;
        fsp->filled[i] = true;
    }

    fsp->completed = fsp->filledCount == fsp->fileSize;
    if (fsp->completed) {
        fsp->cached = std::make_shared<const std::vector<uint8_t>>(std::move(fsp->buffer));
    }
}

std::shared_ptr<const std::vector<uint8_t>> FileCollector::GetFile (uint32_t fileId) const {
    const FileState* fsp;
    {
        std::shared_lock<std::shared_mutex> slock(smtx);
        auto it = files.find(fileId);
        if (it == files.end()) return nullptr;
        fsp = &(it->second);
    }
    std::lock_guard<std::mutex> lock(fsp->mtx);
    if (!fsp->completed || !fsp->cached) return nullptr;

    return fsp->cached;
}

std::optional<std::vector<uint8_t>> FileCollector::GetPartFile (uint32_t fileId, size_t offset, size_t size) const{
    const FileState* fsp; 
    {
        std::shared_lock<std::shared_mutex> slock(smtx);
        auto it = files.find(fileId);
        if (it == files.end()) return std::nullopt;
        fsp = &(it -> second);
    }
    
    if (size == 0) return std::vector<uint8_t>{};

    std::lock_guard<std::mutex> lock(fsp->mtx);
    if (offset > fsp->fileSize || size > fsp->fileSize - offset) return std::nullopt;

    for (size_t i = offset; i < size + offset; ++i) {
        if (!fsp->filled[i]) return std::nullopt;
    }

    const std::vector<uint8_t>& src = (fsp->completed) ? *fsp->cached : fsp->buffer;
    
    return std::vector<uint8_t>(src.begin() + offset, src.begin() + offset + size);
}