#include <gtest/gtest.h>
#include "FileCollector.hpp"
#include <thread>

namespace {
std::vector<uint8_t> bytes(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}
}

TEST(FileCollector, EmptyFileIsCompletedImmediately) {
    FileCollector fc;
    fc.CollectFile(1, 0);
    auto f = fc.GetFile(1);
    ASSERT_TRUE(f.has_value());
    EXPECT_TRUE(f->empty());
}

TEST(FileCollector, GetFileUnknownIdReturnsNullopt) {
    FileCollector fc;
    EXPECT_FALSE(fc.GetFile(42).has_value());
}

TEST(FileCollector, GetFileIncompleteReturnsNullopt) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    EXPECT_FALSE(fc.GetFile(1).has_value());
    fc.OnNewChunk(1, 0, bytes({1, 2, 3}));
    EXPECT_FALSE(fc.GetFile(1).has_value());
}

TEST(FileCollector, SingleChunk) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4}));
    auto f = fc.GetFile(1);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(*f, bytes({1, 2, 3, 4}));
}

TEST(FileCollector, SequentialChunks) {
    FileCollector fc;
    fc.CollectFile(1, 6);
    fc.OnNewChunk(1, 0, bytes({1, 2, 3}));
    fc.OnNewChunk(1, 3, bytes({4, 5, 6}));
    ASSERT_TRUE(fc.GetFile(1).has_value());
    EXPECT_EQ(*fc.GetFile(1), bytes({1, 2, 3, 4, 5, 6}));
}

TEST(FileCollector, OutOfOrderChunks) {
    FileCollector fc;
    fc.CollectFile(1, 6);
    fc.OnNewChunk(1, 3, bytes({4, 5, 6}));
    fc.OnNewChunk(1, 0, bytes({1, 2, 3}));
    ASSERT_TRUE(fc.GetFile(1).has_value());
    EXPECT_EQ(*fc.GetFile(1), bytes({1, 2, 3, 4, 5, 6}));
}

TEST(FileCollector, OverlappingChunksExampleFromTask) {
    FileCollector fc;
    fc.CollectFile(1, 10);
    fc.OnNewChunk(1, 0, bytes({'A', 'B', 'C', 'D'}));
    fc.OnNewChunk(1, 2, bytes({'C', 'D', 'E', 'F'}));
    fc.OnNewChunk(1, 6, bytes({'G', 'H', 'I', 'J'}));
    auto f = fc.GetFile(1);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(*f, bytes({'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'}));
}

TEST(FileCollector, DuplicateChunksAreHarmless) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4}));
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4}));  
    auto f = fc.GetFile(1);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(*f, bytes({1, 2, 3, 4}));
}

TEST(FileCollector, ChunkBeyondFileEndIsClipped) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 2, bytes({3, 4, 5, 6, 7}));  
    EXPECT_FALSE(fc.GetFile(1).has_value());
    fc.OnNewChunk(1, 0, bytes({1, 2}));
    auto f = fc.GetFile(1);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(*f, bytes({1, 2, 3, 4}));
}

TEST(FileCollector, PosAtOrBeyondFileSizeIsIgnored) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 4, bytes({1}));
    fc.OnNewChunk(1, 100, bytes({1}));
    EXPECT_FALSE(fc.GetFile(1).has_value());
}

TEST(FileCollector, EmptyChunkIsIgnored) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 0, {});
    EXPECT_FALSE(fc.GetFile(1).has_value());
}

TEST(FileCollector, ChunkBeforeCollectIsIgnored) {
    FileCollector fc;
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4}));
    EXPECT_FALSE(fc.GetFile(1).has_value());
}

TEST(FileCollector, ChunkAfterCompletionIsIgnored) {
    FileCollector fc;
    fc.CollectFile(1, 2);
    fc.OnNewChunk(1, 0, bytes({1, 2}));
    fc.OnNewChunk(1, 0, bytes({9, 9}));  
    auto f = fc.GetFile(1);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(*f, bytes({1, 2}));
}

TEST(FileCollector, IndependentFilesDoNotInterfere) {
    FileCollector fc;
    fc.CollectFile(1, 2);
    fc.CollectFile(2, 3);
    fc.OnNewChunk(1, 0, bytes({1, 2}));
    fc.OnNewChunk(2, 0, bytes({7, 8, 9}));
    ASSERT_TRUE(fc.GetFile(1).has_value());
    ASSERT_TRUE(fc.GetFile(2).has_value());
    EXPECT_EQ(*fc.GetFile(1), bytes({1, 2}));
    EXPECT_EQ(*fc.GetFile(2), bytes({7, 8, 9}));
}

TEST(FileCollector, MyOwnTest) {
    FileCollector fc;
    fc.CollectFile(1, 16);
    fc.OnNewChunk(1, 0, bytes({1,2,3,4,5}));
    fc.OnNewChunk(1,4,bytes({5,6,4,6,42,18}));
    fc.OnNewChunk(1, 10, bytes({1,2,3,4,5,6}));
    ASSERT_TRUE(fc.GetFile(1).has_value());
    EXPECT_EQ(*fc.GetFile(1), bytes({1,2,3,4,5,6,4,6,42,18,1,2,3,4,5,6}));
}

TEST(FileCollectorMt, ConcurrentChunksOneFile) {
    FileCollector fc;
    fc.CollectFile(1, 1000);
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&, t] {
            for (size_t p = t; p < 1000; p += 8)
                fc.OnNewChunk(1, p, std::vector<uint8_t>{uint8_t(p)});
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_TRUE(fc.GetFile(1).has_value());
}