#include <gtest/gtest.h>
#include "FileCollector.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <thread>
#include <vector>

namespace {

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

// Детерминированное содержимое: байт в позиции i всегда один и тот же.
// Это позволяет побайтово проверять результат независимо от порядка чанков.
uint8_t byteAt(size_t i) {
    return static_cast<uint8_t>((i * 131 + 17) & 0xFF);
}

std::vector<uint8_t> expectedOfSize(size_t n) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = byteAt(i);
    return v;
}

std::vector<uint8_t> makeChunk(size_t pos, size_t len) {
    std::vector<uint8_t> c(len);
    for (size_t j = 0; j < len; ++j) c[j] = byteAt(pos + j);
    return c;
}

void expectContentsEqual(const std::shared_ptr<const std::vector<uint8_t>>& got,
                         const std::vector<uint8_t>& want) {
    ASSERT_NE(got, nullptr) << "GetFile returned nullptr";
    ASSERT_EQ(got->size(), want.size()) << "size mismatch";
    for (size_t i = 0; i < want.size(); ++i) {
        ASSERT_EQ((*got)[i], want[i]) << "byte mismatch at index " << i;
    }
}

} // namespace

// ============================================================
// Basic correctness
// ============================================================

TEST(FileCollector, EmptyFileIsCompletedImmediately) {
    FileCollector fc;
    fc.CollectFile(1, 0);
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_TRUE(f->empty());
}

TEST(FileCollector, GetFileUnknownIdReturnsNullptr) {
    FileCollector fc;
    EXPECT_EQ(fc.GetFile(42), nullptr);
}

TEST(FileCollector, GetFileIncompleteReturnsNullptr) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    EXPECT_EQ(fc.GetFile(1), nullptr);
    fc.OnNewChunk(1, 0, bytes({1, 2, 3}));
    EXPECT_EQ(fc.GetFile(1), nullptr);
}

TEST(FileCollector, SingleChunk) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4}));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(*f, bytes({1, 2, 3, 4}));
}

TEST(FileCollector, SequentialChunks) {
    FileCollector fc;
    fc.CollectFile(1, 6);
    fc.OnNewChunk(1, 0, bytes({1, 2, 3}));
    fc.OnNewChunk(1, 3, bytes({4, 5, 6}));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(*f, bytes({1, 2, 3, 4, 5, 6}));
}

TEST(FileCollector, OutOfOrderChunks) {
    FileCollector fc;
    fc.CollectFile(1, 6);
    fc.OnNewChunk(1, 3, bytes({4, 5, 6}));
    fc.OnNewChunk(1, 0, bytes({1, 2, 3}));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(*f, bytes({1, 2, 3, 4, 5, 6}));
}

TEST(FileCollector, OverlappingChunksExampleFromTask) {
    FileCollector fc;
    fc.CollectFile(1, 10);
    fc.OnNewChunk(1, 0, bytes({'A', 'B', 'C', 'D'}));
    fc.OnNewChunk(1, 2, bytes({'C', 'D', 'E', 'F'}));
    fc.OnNewChunk(1, 6, bytes({'G', 'H', 'I', 'J'}));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(*f, bytes({'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'}));
}

TEST(FileCollector, DuplicateChunksAreHarmless) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4}));
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4}));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(*f, bytes({1, 2, 3, 4}));
}

TEST(FileCollector, ChunkBeyondFileEndIsClipped) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 2, bytes({3, 4, 5, 6, 7}));
    EXPECT_EQ(fc.GetFile(1), nullptr);
    fc.OnNewChunk(1, 0, bytes({1, 2}));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(*f, bytes({1, 2, 3, 4}));
}

TEST(FileCollector, PosAtOrBeyondFileSizeIsIgnored) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 4, bytes({1}));
    fc.OnNewChunk(1, 100, bytes({1}));
    EXPECT_EQ(fc.GetFile(1), nullptr);
}

TEST(FileCollector, EmptyChunkIsIgnored) {
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 0, {});
    EXPECT_EQ(fc.GetFile(1), nullptr);
}

TEST(FileCollector, ChunkBeforeCollectIsIgnored) {
    FileCollector fc;
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4}));
    EXPECT_EQ(fc.GetFile(1), nullptr);
}

TEST(FileCollector, ChunkAfterCompletionIsIgnored) {
    FileCollector fc;
    fc.CollectFile(1, 2);
    fc.OnNewChunk(1, 0, bytes({1, 2}));
    fc.OnNewChunk(1, 0, bytes({9, 9}));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(*f, bytes({1, 2}));
}

TEST(FileCollector, IndependentFilesDoNotInterfere) {
    FileCollector fc;
    fc.CollectFile(1, 2);
    fc.CollectFile(2, 3);
    fc.OnNewChunk(1, 0, bytes({1, 2}));
    fc.OnNewChunk(2, 0, bytes({7, 8, 9}));
    auto f1 = fc.GetFile(1);
    auto f2 = fc.GetFile(2);
    ASSERT_NE(f1, nullptr);
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(*f1, bytes({1, 2}));
    EXPECT_EQ(*f2, bytes({7, 8, 9}));
}

TEST(FileCollector, CustomOverlaps) {
    FileCollector fc;
    fc.CollectFile(1, 16);
    fc.OnNewChunk(1, 0, bytes({1, 2, 3, 4, 5}));
    fc.OnNewChunk(1, 4, bytes({5, 6, 4, 6, 42, 18}));
    fc.OnNewChunk(1, 10, bytes({1, 2, 3, 4, 5, 6}));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(*f, bytes({1, 2, 3, 4, 5, 6, 4, 6, 42, 18, 1, 2, 3, 4, 5, 6}));
}


TEST(FileCollectorStress, OverlapCounterMustNotDoubleCount) {
    FileCollector fc;
    fc.CollectFile(1, 100);
    fc.OnNewChunk(1, 0, makeChunk(0, 60));
    fc.OnNewChunk(1, 0, makeChunk(0, 60)); // полный дубль
    EXPECT_EQ(fc.GetFile(1), nullptr) << "must stay incomplete: only [0,60) covered";
    fc.OnNewChunk(1, 60, makeChunk(60, 40));
    expectContentsEqual(fc.GetFile(1), expectedOfSize(100));
}

TEST(FileCollectorStress, ConcurrentOverlappingChunksContentCheck) {
    constexpr size_t SIZE = 4096;
    constexpr int THREADS = 8;
    constexpr int CHUNKS_PER_THREAD = 200;

    FileCollector fc;
    fc.CollectFile(7, SIZE);

    std::vector<std::thread> writers;
    for (int t = 0; t < THREADS; ++t) {
        writers.emplace_back([&fc, t] {
            std::mt19937 rng(0xC0FFEE + t);
            std::uniform_int_distribution<size_t> posDist(0, SIZE - 1);
            std::uniform_int_distribution<size_t> lenDist(1, 512);
            for (int i = 0; i < CHUNKS_PER_THREAD; ++i) {
                size_t pos = posDist(rng);
                size_t len = std::min<size_t>(lenDist(rng), SIZE - pos);
                fc.OnNewChunk(7, pos, makeChunk(pos, len));
            }
        });
    }

    std::atomic<bool> stop{false};
    std::atomic<int> wrongReads{0};
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            auto f = fc.GetFile(7);
            if (!f) continue;
            if (f->size() != SIZE) { ++wrongReads; continue; }
            for (size_t i = 0; i < SIZE; ++i) {
                if ((*f)[i] != byteAt(i)) { ++wrongReads; break; }
            }
        }
    });

    for (auto& w : writers) w.join();
    stop = true;
    reader.join();

    EXPECT_EQ(wrongReads.load(), 0) << "reader saw partial/invalid file";
    expectContentsEqual(fc.GetFile(7), expectedOfSize(SIZE));
}

TEST(FileCollectorStress, SamePositionManyChunksNoDoubleCount) {
    FileCollector fc;
    fc.CollectFile(1, 100);
    std::vector<std::thread> ts;
    for (int i = 0; i < 100; ++i) {
        ts.emplace_back([&fc] { fc.OnNewChunk(1, 0, makeChunk(0, 50)); });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(fc.GetFile(1), nullptr) << "gap [50,100) must keep file incomplete";
    fc.OnNewChunk(1, 50, makeChunk(50, 50));
    expectContentsEqual(fc.GetFile(1), expectedOfSize(100));
}

TEST(FileCollectorStress, ConcurrentLastByteWrite) {
    for (int iter = 0; iter < 100; ++iter) {
        FileCollector fc;
        constexpr size_t SIZE = 100;
        fc.CollectFile(1, SIZE);
        fc.OnNewChunk(1, 0, makeChunk(0, SIZE - 1));
        std::vector<std::thread> ts;
        for (int t = 0; t < 8; ++t) {
            ts.emplace_back([&fc] { fc.OnNewChunk(1, 99, makeChunk(99, 1)); });
        }
        for (auto& t : ts) t.join();
        expectContentsEqual(fc.GetFile(1), expectedOfSize(SIZE));
    }
}

TEST(FileCollectorStress, ManyFilesInParallel) {
    constexpr int FILES = 20;
    constexpr int THREADS_PER_FILE = 4;
    constexpr size_t SIZE = 512;

    FileCollector fc;
    for (int f = 0; f < FILES; ++f) fc.CollectFile(f, SIZE);

    std::vector<std::thread> ts;
    for (int f = 0; f < FILES; ++f) {
        for (int t = 0; t < THREADS_PER_FILE; ++t) {
            ts.emplace_back([&fc, f, t] {
                for (size_t p = static_cast<size_t>(t); p < SIZE;
                     p += THREADS_PER_FILE) {
                    fc.OnNewChunk(f, p, makeChunk(p, 1));
                }
            });
        }
    }
    for (auto& t : ts) t.join();

    for (int f = 0; f < FILES; ++f) {
        expectContentsEqual(fc.GetFile(f), expectedOfSize(SIZE));
    }
}

// Хаос: одновременно пишем, читаем, сбрасываем. Критерий — не упало,
// TSan чист, нет дедлока. Контент не проверяем (файл 0 сбрасывается).
TEST(FileCollectorStress, ChaosCollectChunkRead) {
    FileCollector fc;
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&fc, &stop, t] {
            std::mt19937 rng(t);
            while (!stop.load(std::memory_order_relaxed)) {
                uint32_t id = rng() % 4;
                size_t pos = rng() % 100;
                size_t len = 1 + rng() % 50;
                fc.OnNewChunk(id, pos, makeChunk(pos, len));
            }
        });
    }
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&fc, &stop] {
            while (!stop.load(std::memory_order_relaxed)) fc.CollectFile(0, 100);
        });
    }
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&fc, &stop] {
            while (!stop.load(std::memory_order_relaxed)) {
                (void)fc.GetFile(1);
                (void)fc.GetFile(2);
                (void)fc.GetFile(99);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;
    for (auto& t : threads) t.join();
    SUCCEED();
}

TEST(FileCollectorStress, GetFileStormDuringCompletion) {
    constexpr size_t SIZE = 10'000;
    FileCollector fc;
    fc.CollectFile(1, SIZE);

    std::thread writer([&] {
        for (size_t i = 0; i < SIZE; i += 100) {
            size_t len = std::min<size_t>(100, SIZE - i);
            fc.OnNewChunk(1, i, makeChunk(i, len));
        }
    });

    std::atomic<int> mismatches{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&] {
            for (int i = 0; i < 1000; ++i) {
                auto f = fc.GetFile(1);
                if (!f) continue;
                if (f->size() != SIZE) { ++mismatches; continue; }
                for (size_t j = 0; j < SIZE; ++j) {
                    if ((*f)[j] != byteAt(j)) { ++mismatches; break; }
                }
            }
        });
    }

    writer.join();
    for (auto& r : readers) r.join();
    EXPECT_EQ(mismatches.load(), 0);
    expectContentsEqual(fc.GetFile(1), expectedOfSize(SIZE));
}

// GetFile должен возвращать тот же объект, а не новую копию.
TEST(FileCollectorStress, GetFileReturnsSameObject) {
    FileCollector fc;
    fc.CollectFile(1, 100);
    fc.OnNewChunk(1, 0, makeChunk(0, 100));
    auto f1 = fc.GetFile(1);
    auto f2 = fc.GetFile(1);
    ASSERT_NE(f1, nullptr);
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(f1.get(), f2.get()) << "GetFile must return the same cached object";
}

TEST(FileCollectorStress, RepeatedCollectFileResetsState) {
    FileCollector fc;
    fc.CollectFile(1, 10);
    fc.OnNewChunk(1, 0, makeChunk(0, 10));
    expectContentsEqual(fc.GetFile(1), expectedOfSize(10));

    fc.CollectFile(1, 10);
    EXPECT_EQ(fc.GetFile(1), nullptr) << "state must be reset";

    std::vector<uint8_t> ff(10, 0xFF);
    fc.OnNewChunk(1, 0, std::move(ff));
    auto f = fc.GetFile(1);
    ASSERT_NE(f, nullptr);
    for (size_t i = 0; i < 10; ++i) EXPECT_EQ((*f)[i], 0xFF);
}

TEST(FileCollectorStress, HugeChunkClippedToFileSize) {
    FileCollector fc;
    fc.CollectFile(1, 10);
    std::vector<uint8_t> huge(1'000'000);
    for (size_t i = 0; i < huge.size(); ++i) huge[i] = byteAt(i);
    fc.OnNewChunk(1, 0, std::move(huge));
    expectContentsEqual(fc.GetFile(1), expectedOfSize(10));
}

TEST(FileCollectorStress, HugePosIsIgnored) {
    FileCollector fc;
    fc.CollectFile(1, 100);
    fc.OnNewChunk(1, SIZE_MAX, makeChunk(0, 3));
    fc.OnNewChunk(1, SIZE_MAX - 1, makeChunk(0, 3));
    EXPECT_EQ(fc.GetFile(1), nullptr);
}

TEST(FileCollectorStress, ChainOverlapsByteVerified) {
    constexpr size_t SIZE = 500;
    constexpr size_t LEN = 10;
    FileCollector fc;
    fc.CollectFile(1, SIZE);
    for (size_t i = 0; i + LEN <= SIZE; ++i) {
        fc.OnNewChunk(1, i, makeChunk(i, LEN));
    }
    expectContentsEqual(fc.GetFile(1), expectedOfSize(SIZE));
}

TEST(FileCollectorStress, RandomChaosContent) {
    std::mt19937 rng(42);
    for (int iter = 0; iter < 50; ++iter) {
        size_t sz = 1 + rng() % 500;
        FileCollector fc;
        fc.CollectFile(1, sz);

        int nchunks = 1 + static_cast<int>(rng() % 100);
        for (int i = 0; i < nchunks; ++i) {
            size_t pos = rng() % sz;
            size_t len = 1 + rng() % (sz - pos);
            fc.OnNewChunk(1, pos, makeChunk(pos, len));
        }
        // Гарантированно добиваем все позиции
        for (size_t i = 0; i < sz; ++i) {
            fc.OnNewChunk(1, i, makeChunk(i, 1));
        }
        expectContentsEqual(fc.GetFile(1), expectedOfSize(sz));
    }
}

TEST(FileCollectorStress, ManyTinyFiles) {
    constexpr int N = 1000;
    FileCollector fc;
    for (int i = 0; i < N; ++i) {
        fc.CollectFile(i, 1);
        fc.OnNewChunk(i, 0, makeChunk(0, 1));
    }
    for (int i = 0; i < N; ++i) {
        auto f = fc.GetFile(i);
        ASSERT_NE(f, nullptr);
        ASSERT_EQ(f->size(), 1);
        EXPECT_EQ((*f)[0], byteAt(0));
    }
}

TEST(FileCollectorStress, LargeFileManyWriters) {
    constexpr size_t SIZE = 100'000;
    constexpr int THREADS = 8;
    constexpr size_t CHUNK = 1000;

    FileCollector fc;
    fc.CollectFile(42, SIZE);

    std::vector<std::thread> ts;
    for (int t = 0; t < THREADS; ++t) {
        ts.emplace_back([&fc, t] {
            constexpr size_t chunk = CHUNK;
            for (size_t base = static_cast<size_t>(t) * chunk; base < SIZE;
                 base += chunk * THREADS) {
                size_t len = std::min<size_t>(chunk, SIZE - base);
                fc.OnNewChunk(42, base, makeChunk(base, len));
            }
        });
    }
    for (auto& t : ts) t.join();
    expectContentsEqual(fc.GetFile(42), expectedOfSize(SIZE));
}

TEST(FileCollectorStress, ZeroLengthChunkIgnored) {
    FileCollector fc;
    fc.CollectFile(1, 100);
    for (int i = 0; i < 1000; ++i) {
        fc.OnNewChunk(1, static_cast<size_t>(i) % 100, std::vector<uint8_t>{});
    }
    EXPECT_EQ(fc.GetFile(1), nullptr);
}

TEST(FileCollectorStress, EmptyFileManyThreads) {
    FileCollector fc;
    std::vector<std::thread> ts;
    for (int i = 0; i < 10; ++i) {
        ts.emplace_back([&fc] {
            for (int j = 0; j < 100; ++j) {
                fc.CollectFile(1, 0);
                auto f = fc.GetFile(1);
                if (f) {
                    EXPECT_TRUE(f->empty());
                }
            }
        });
    }
    for (auto& t : ts) t.join();
}

TEST(FileCollectorStress, CollectFileDuringWriteDoesNotCrash) {
    FileCollector fc;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            fc.OnNewChunk(1, 0, makeChunk(0, 3));
        }
    });
    std::thread collector([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            fc.CollectFile(1, 100);
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop = true;
    writer.join();
    collector.join();
    SUCCEED();
}