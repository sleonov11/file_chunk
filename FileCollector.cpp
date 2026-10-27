#include "FileCollector.hpp"

#include <cstring>
#include <algorithm>

void FileCollector::CollectFile(uint32_t fileId, size_t fileSize) {
    FileState* fsp;
    std::vector<uint8_t> newBuffer(fileSize, 0);
    std::vector<uint8_t> newFilled(fileSize, false); // if bad-alloc. it couldnt be under lock, so it provides some safety))
    std::shared_ptr<const std::vector<uint8_t>> newCached;
    if (fileSize == 0) newCached = std::make_shared<const std::vector<uint8_t>>();
    {
    std::unique_lock<std::shared_mutex> slock(smtx);
    fsp = &files[fileId];
    }
    std::unique_lock<std::shared_mutex> lock(fsp->mtx);
    fsp->fileSize = fileSize;
    fsp->buffer = std::move(newBuffer); // for safety
    fsp->filled = std::move(newFilled);
    fsp->filledCount = 0;
    fsp->cached = std::move(newCached);
    fsp->completed = (fileSize == 0);
}

void FileCollector::OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk) {
    FileState* fsp;
    {
    std::shared_lock<std::shared_mutex> slock(smtx);
    auto it = files.find(fileId);
    
    if (it == files.end()) return;
    
    fsp = &it->second;
    }
    std::unique_lock<std::shared_mutex> lock(fsp->mtx);
    if (fsp->completed) return;

    if (chunk.size() == 0) return;
    if (pos >= fsp->fileSize) return;

    size_t len = std::min(chunk.size(), fsp->fileSize - pos);
    
    std::memcpy(fsp->buffer.data() + pos, chunk.data(), len);

    size_t endPos = len + pos;
    for (size_t i = pos; i < endPos; ++i) {
        bool wasNotFilled = !fsp->filled[i];
        fsp->filledCount += wasNotFilled;
        fsp->filled[i] = true;
    }

    if (fsp->filledCount==fsp->fileSize && !fsp->completed) {
        fsp->cached = std::make_shared<const std::vector<uint8_t>>(std::move(fsp->buffer));
        fsp->completed = true;
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
    std::shared_lock<std::shared_mutex> lock(fsp->mtx);
    if (!fsp->completed || !fsp->cached) return nullptr;

    return fsp->cached;
}

// returns a copy of a part of the file in range [offset, offset+size]
// return std::optional<std::vector<uint8_t>> containing the copied data
// returns std::nullopt if:
//   - the file ID is undefined
//   - the requested range exceeds the file size
//   - at least one byte in the requested range has not been received yet
// returns a copy (not a view or pointer) to ensure memory safety,
//  as the internal buffer may be modified or reset by concurrent CollectFile calls
std::optional<std::vector<uint8_t>> FileCollector::GetPartFile (uint32_t fileId, size_t offset, size_t size) const{
    const FileState* fsp; 
    {
        std::shared_lock<std::shared_mutex> slock(smtx);
        auto it = files.find(fileId);
        if (it == files.end()) return std::nullopt;
        fsp = &(it -> second);
    }

    std::shared_lock<std::shared_mutex> lock(fsp->mtx);
    if (offset > fsp->fileSize || size > fsp->fileSize - offset) return std::nullopt;
    if (size == 0) return std::vector<uint8_t>{};


    for (size_t i = offset; i < size + offset; ++i) {
        if (!fsp->filled[i]) return std::nullopt;
    }

    const std::vector<uint8_t>& src = (fsp->completed) ? *fsp->cached : fsp->buffer;
    
    return std::vector<uint8_t>(src.begin() + offset, src.begin() + offset + size);
}