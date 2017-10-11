/*
 * Copyright 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <C2Buffer.h>
#include <C2BufferPriv.h>
#include <C2ParamDef.h>

#include <system/graphics.h>

namespace android {

class C2BufferTest : public ::testing::Test {
public:
    C2BufferTest()
        : mLinearAllocator(std::make_shared<C2AllocatorIon>()),
          mSize(0u),
          mAddr(nullptr),
          mGraphicAllocator(std::make_shared<C2AllocatorGralloc>()) {
    }

    ~C2BufferTest() = default;

    void allocateLinear(size_t capacity) {
        C2Error err = mLinearAllocator->allocateLinearBuffer(
                capacity,
                { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
                &mLinearAllocation);
        if (err != C2_OK) {
            mLinearAllocation.reset();
            FAIL() << "C2Allocator::allocateLinearBuffer() failed: " << err;
        }
    }

    void mapLinear(size_t offset, size_t size, uint8_t **addr) {
        ASSERT_TRUE(mLinearAllocation);
        C2Error err = mLinearAllocation->map(
                offset,
                size,
                { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
                // TODO: fence
                nullptr,
                &mAddr);
        if (err != C2_OK) {
            mAddr = nullptr;
            FAIL() << "C2LinearAllocation::map() failed: " << err;
        }
        ASSERT_NE(nullptr, mAddr);
        mSize = size;
        *addr = (uint8_t *)mAddr;
    }

    void unmapLinear() {
        ASSERT_TRUE(mLinearAllocation);
        ASSERT_NE(nullptr, mAddr);
        ASSERT_NE(0u, mSize);

        // TODO: fence
        ASSERT_EQ(C2_OK, mLinearAllocation->unmap(mAddr, mSize, nullptr));
        mSize = 0u;
        mAddr = nullptr;
    }

    std::shared_ptr<C2BlockAllocator> makeLinearBlockAllocator() {
        return std::make_shared<C2DefaultBlockAllocator>(mLinearAllocator);
    }

    void allocateGraphic(uint32_t width, uint32_t height) {
        C2Error err = mGraphicAllocator->allocateGraphicBuffer(
                width,
                height,
                HAL_PIXEL_FORMAT_YCBCR_420_888,
                { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
                &mGraphicAllocation);
        if (err != C2_OK) {
            mGraphicAllocation.reset();
            FAIL() << "C2Allocator::allocateLinearBuffer() failed: " << err;
        }
    }

    void mapGraphic(C2Rect rect, C2PlaneLayout *layout, uint8_t **addr) {
        ASSERT_TRUE(mGraphicAllocation);
        C2Error err = mGraphicAllocation->map(
                rect,
                { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
                // TODO: fence
                nullptr,
                layout,
                addr);
        if (err != C2_OK) {
            addr[C2PlaneLayout::Y] = nullptr;
            addr[C2PlaneLayout::U] = nullptr;
            addr[C2PlaneLayout::V] = nullptr;
            FAIL() << "C2GraphicAllocation::map() failed: " << err;
        }
    }

    void unmapGraphic() {
        ASSERT_TRUE(mGraphicAllocation);

        // TODO: fence
        ASSERT_EQ(C2_OK, mGraphicAllocation->unmap(nullptr));
    }

    std::shared_ptr<C2BlockAllocator> makeGraphicBlockAllocator() {
        return std::make_shared<C2DefaultGraphicBlockAllocator>(mGraphicAllocator);
    }

private:
    std::shared_ptr<C2Allocator> mLinearAllocator;
    std::shared_ptr<C2LinearAllocation> mLinearAllocation;
    size_t mSize;
    void *mAddr;

    std::shared_ptr<C2Allocator> mGraphicAllocator;
    std::shared_ptr<C2GraphicAllocation> mGraphicAllocation;
};

TEST_F(C2BufferTest, LinearAllocationTest) {
    constexpr size_t kCapacity = 1024u * 1024u;

    allocateLinear(kCapacity);

    uint8_t *addr = nullptr;
    mapLinear(0u, kCapacity, &addr);
    ASSERT_NE(nullptr, addr);

    for (size_t i = 0; i < kCapacity; ++i) {
        addr[i] = i % 100u;
    }

    unmapLinear();
    addr = nullptr;

    mapLinear(kCapacity / 3, kCapacity / 3, &addr);
    ASSERT_NE(nullptr, addr);
    for (size_t i = 0; i < kCapacity / 3; ++i) {
        ASSERT_EQ((i + kCapacity / 3) % 100, addr[i]) << " at i = " << i;
    }
}

TEST_F(C2BufferTest, BlockAllocatorTest) {
    constexpr size_t kCapacity = 1024u * 1024u;

    std::shared_ptr<C2BlockAllocator> blockAllocator(makeLinearBlockAllocator());

    std::shared_ptr<C2LinearBlock> block;
    ASSERT_EQ(C2_OK, blockAllocator->allocateLinearBlock(
            kCapacity,
            { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
            &block));
    ASSERT_TRUE(block);

    C2Acquirable<C2WriteView> writeViewHolder = block->map();
    C2WriteView writeView = writeViewHolder.get();
    ASSERT_EQ(C2_OK, writeView.error());
    ASSERT_EQ(kCapacity, writeView.capacity());
    ASSERT_EQ(0u, writeView.offset());
    ASSERT_EQ(kCapacity, writeView.size());

    uint8_t *data = writeView.data();
    ASSERT_NE(nullptr, data);
    for (size_t i = 0; i < writeView.size(); ++i) {
        data[i] = i % 100u;
    }

    C2Fence fence;
    C2ConstLinearBlock constBlock = block->share(
            kCapacity / 3, kCapacity / 3, fence);

    C2Acquirable<C2ReadView> readViewHolder = constBlock.map();
    C2ReadView readView = readViewHolder.get();
    ASSERT_EQ(C2_OK, readView.error());
    ASSERT_EQ(kCapacity / 3, readView.capacity());

    // TODO: fence
    const uint8_t *constData = readView.data();
    ASSERT_NE(nullptr, constData);
    for (size_t i = 0; i < readView.capacity(); ++i) {
        ASSERT_EQ((i + kCapacity / 3) % 100u, constData[i]) << " at i = " << i
                << "; data = " << static_cast<void *>(data)
                << "; constData = " << static_cast<const void *>(constData);
    }

    readView = readView.subView(333u, 100u);
    ASSERT_EQ(C2_OK, readView.error());
    ASSERT_EQ(100u, readView.capacity());

    constData = readView.data();
    ASSERT_NE(nullptr, constData);
    for (size_t i = 0; i < readView.capacity(); ++i) {
        ASSERT_EQ((i + 333u + kCapacity / 3) % 100u, constData[i]) << " at i = " << i;
    }
}

void fillPlane(const C2Rect rect, const C2PlaneInfo info, uint8_t *addr, uint8_t value) {
    for (uint32_t row = 0; row < rect.mHeight / info.mVertSubsampling; ++row) {
        int32_t rowOffset = (row + rect.mTop / info.mVertSubsampling) * info.mRowInc;
        for (uint32_t col = 0; col < rect.mWidth / info.mHorizSubsampling; ++col) {
            int32_t colOffset = (col + rect.mLeft / info.mHorizSubsampling) * info.mColInc;
            addr[rowOffset + colOffset] = value;
        }
    }
}

bool verifyPlane(const C2Rect rect, const C2PlaneInfo info, const uint8_t *addr, uint8_t value) {
    for (uint32_t row = 0; row < rect.mHeight / info.mVertSubsampling; ++row) {
        int32_t rowOffset = (row + rect.mTop / info.mVertSubsampling) * info.mRowInc;
        for (uint32_t col = 0; col < rect.mWidth / info.mHorizSubsampling; ++col) {
            int32_t colOffset = (col + rect.mLeft / info.mHorizSubsampling) * info.mColInc;
            if (addr[rowOffset + colOffset] != value) {
                return false;
            }
        }
    }
    return true;
}

TEST_F(C2BufferTest, GraphicAllocationTest) {
    constexpr uint32_t kWidth = 320;
    constexpr uint32_t kHeight = 240;

    allocateGraphic(kWidth, kHeight);

    uint8_t *addr[C2PlaneLayout::MAX_NUM_PLANES];
    C2Rect rect{ 0, 0, kWidth, kHeight };
    C2PlaneLayout layout;
    mapGraphic(rect, &layout, addr);
    ASSERT_NE(nullptr, addr[C2PlaneLayout::Y]);
    ASSERT_NE(nullptr, addr[C2PlaneLayout::U]);
    ASSERT_NE(nullptr, addr[C2PlaneLayout::V]);

    uint8_t *y = addr[C2PlaneLayout::Y];
    C2PlaneInfo yInfo = layout.mPlanes[C2PlaneLayout::Y];
    uint8_t *u = addr[C2PlaneLayout::U];
    C2PlaneInfo uInfo = layout.mPlanes[C2PlaneLayout::U];
    uint8_t *v = addr[C2PlaneLayout::V];
    C2PlaneInfo vInfo = layout.mPlanes[C2PlaneLayout::V];

    fillPlane(rect, yInfo, y, 0);
    fillPlane(rect, uInfo, u, 0);
    fillPlane(rect, vInfo, v, 0);
    fillPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, yInfo, y, 0x12);
    fillPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, uInfo, u, 0x34);
    fillPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, vInfo, v, 0x56);

    unmapGraphic();

    mapGraphic(rect, &layout, addr);
    ASSERT_NE(nullptr, addr[C2PlaneLayout::Y]);
    ASSERT_NE(nullptr, addr[C2PlaneLayout::U]);
    ASSERT_NE(nullptr, addr[C2PlaneLayout::V]);

    y = addr[C2PlaneLayout::Y];
    yInfo = layout.mPlanes[C2PlaneLayout::Y];
    u = addr[C2PlaneLayout::U];
    uInfo = layout.mPlanes[C2PlaneLayout::U];
    v = addr[C2PlaneLayout::V];
    vInfo = layout.mPlanes[C2PlaneLayout::V];

    ASSERT_TRUE(verifyPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, yInfo, y, 0x12));
    ASSERT_TRUE(verifyPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, uInfo, u, 0x34));
    ASSERT_TRUE(verifyPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, vInfo, v, 0x56));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth, kHeight / 4 }, yInfo, y, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth, kHeight / 4 }, uInfo, u, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth, kHeight / 4 }, vInfo, v, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth / 4, kHeight }, yInfo, y, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth / 4, kHeight }, uInfo, u, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth / 4, kHeight }, vInfo, v, 0));
}

TEST_F(C2BufferTest, GraphicBlockAllocatorTest) {
    constexpr uint32_t kWidth = 320;
    constexpr uint32_t kHeight = 240;

    std::shared_ptr<C2BlockAllocator> blockAllocator(makeGraphicBlockAllocator());

    std::shared_ptr<C2GraphicBlock> block;
    ASSERT_EQ(C2_OK, blockAllocator->allocateGraphicBlock(
            kWidth,
            kHeight,
            HAL_PIXEL_FORMAT_YCBCR_420_888,
            { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
            &block));
    ASSERT_TRUE(block);

    C2Acquirable<C2GraphicView> graphicViewHolder = block->map();
    C2GraphicView graphicView = graphicViewHolder.get();
    ASSERT_EQ(C2_OK, graphicView.error());
    ASSERT_EQ(kWidth, graphicView.width());
    ASSERT_EQ(kHeight, graphicView.height());

    uint8_t *const *data = graphicView.data();
    C2PlaneLayout layout = graphicView.layout();
    ASSERT_NE(nullptr, data);

    uint8_t *y = data[C2PlaneLayout::Y];
    C2PlaneInfo yInfo = layout.mPlanes[C2PlaneLayout::Y];
    uint8_t *u = data[C2PlaneLayout::U];
    C2PlaneInfo uInfo = layout.mPlanes[C2PlaneLayout::U];
    uint8_t *v = data[C2PlaneLayout::V];
    C2PlaneInfo vInfo = layout.mPlanes[C2PlaneLayout::V];

    fillPlane({ 0, 0, kWidth, kHeight }, yInfo, y, 0);
    fillPlane({ 0, 0, kWidth, kHeight }, uInfo, u, 0);
    fillPlane({ 0, 0, kWidth, kHeight }, vInfo, v, 0);
    fillPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, yInfo, y, 0x12);
    fillPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, uInfo, u, 0x34);
    fillPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, vInfo, v, 0x56);

    C2Fence fence;
    C2ConstGraphicBlock constBlock = block->share(
            { 0, 0, kWidth, kHeight }, fence);
    block.reset();

    C2Acquirable<const C2GraphicView> constViewHolder = constBlock.map();
    const C2GraphicView constGraphicView = constViewHolder.get();
    ASSERT_EQ(C2_OK, constGraphicView.error());
    ASSERT_EQ(kWidth, constGraphicView.width());
    ASSERT_EQ(kHeight, constGraphicView.height());

    const uint8_t *const *constData = constGraphicView.data();
    layout = graphicView.layout();
    ASSERT_NE(nullptr, constData);

    const uint8_t *cy = constData[C2PlaneLayout::Y];
    yInfo = layout.mPlanes[C2PlaneLayout::Y];
    const uint8_t *cu = constData[C2PlaneLayout::U];
    uInfo = layout.mPlanes[C2PlaneLayout::U];
    const uint8_t *cv = constData[C2PlaneLayout::V];
    vInfo = layout.mPlanes[C2PlaneLayout::V];

    ASSERT_TRUE(verifyPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, yInfo, cy, 0x12));
    ASSERT_TRUE(verifyPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, uInfo, cu, 0x34));
    ASSERT_TRUE(verifyPlane({ kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2 }, vInfo, cv, 0x56));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth, kHeight / 4 }, yInfo, cy, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth, kHeight / 4 }, uInfo, cu, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth, kHeight / 4 }, vInfo, cv, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth / 4, kHeight }, yInfo, cy, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth / 4, kHeight }, uInfo, cu, 0));
    ASSERT_TRUE(verifyPlane({ 0, 0, kWidth / 4, kHeight }, vInfo, cv, 0));
}

class BufferData : public C2BufferData {
public:
    explicit BufferData(const std::list<C2ConstLinearBlock> &blocks) : C2BufferData(blocks) {}
    explicit BufferData(const std::list<C2ConstGraphicBlock> &blocks) : C2BufferData(blocks) {}
};

class Buffer : public C2Buffer {
public:
    explicit Buffer(const std::list<C2ConstLinearBlock> &blocks) : C2Buffer(blocks) {}
    explicit Buffer(const std::list<C2ConstGraphicBlock> &blocks) : C2Buffer(blocks) {}
};

TEST_F(C2BufferTest, BufferDataTest) {
    std::shared_ptr<C2BlockAllocator> linearBlockAllocator(makeLinearBlockAllocator());
    std::shared_ptr<C2BlockAllocator> graphicBlockAllocator(makeGraphicBlockAllocator());

    constexpr uint32_t kWidth1 = 320;
    constexpr uint32_t kHeight1 = 240;
    constexpr C2Rect kCrop1(kWidth1, kHeight1);
    constexpr uint32_t kWidth2 = 176;
    constexpr uint32_t kHeight2 = 144;
    constexpr C2Rect kCrop2(kWidth2, kHeight2);
    constexpr size_t kCapacity1 = 1024u;
    constexpr size_t kCapacity2 = 2048u;

    std::shared_ptr<C2LinearBlock> linearBlock1;
    std::shared_ptr<C2LinearBlock> linearBlock2;
    ASSERT_EQ(C2_OK, linearBlockAllocator->allocateLinearBlock(
            kCapacity1,
            { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
            &linearBlock1));
    ASSERT_EQ(C2_OK, linearBlockAllocator->allocateLinearBlock(
            kCapacity2,
            { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
            &linearBlock2));
    std::shared_ptr<C2GraphicBlock> graphicBlock1;
    std::shared_ptr<C2GraphicBlock> graphicBlock2;
    ASSERT_EQ(C2_OK, graphicBlockAllocator->allocateGraphicBlock(
            kWidth1,
            kHeight1,
            HAL_PIXEL_FORMAT_YCBCR_420_888,
            { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
            &graphicBlock1));
    ASSERT_EQ(C2_OK, graphicBlockAllocator->allocateGraphicBlock(
            kWidth2,
            kHeight2,
            HAL_PIXEL_FORMAT_YCBCR_420_888,
            { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
            &graphicBlock2));

    std::shared_ptr<C2BufferData> data(new BufferData({ linearBlock1->share(0, kCapacity1, C2Fence()) }));
    EXPECT_EQ(C2BufferData::LINEAR, data->type());
    ASSERT_EQ(1u, data->linearBlocks().size());
    EXPECT_EQ(linearBlock1->handle(), data->linearBlocks().front().handle());
    EXPECT_TRUE(data->graphicBlocks().empty());

    data.reset(new BufferData({
        linearBlock1->share(0, kCapacity1, C2Fence()),
        linearBlock2->share(0, kCapacity2, C2Fence()),
    }));
    EXPECT_EQ(C2BufferData::LINEAR_CHUNKS, data->type());
    ASSERT_EQ(2u, data->linearBlocks().size());
    EXPECT_EQ(linearBlock1->handle(), data->linearBlocks().front().handle());
    EXPECT_EQ(linearBlock2->handle(), data->linearBlocks().back().handle());
    EXPECT_TRUE(data->graphicBlocks().empty());

    data.reset(new BufferData({ graphicBlock1->share(kCrop1, C2Fence()) }));
    EXPECT_EQ(C2BufferData::GRAPHIC, data->type());
    ASSERT_EQ(1u, data->graphicBlocks().size());
    EXPECT_EQ(graphicBlock1->handle(), data->graphicBlocks().front().handle());
    EXPECT_TRUE(data->linearBlocks().empty());

    data.reset(new BufferData({
        graphicBlock1->share(kCrop1, C2Fence()),
        graphicBlock2->share(kCrop2, C2Fence()),
    }));
    EXPECT_EQ(C2BufferData::GRAPHIC_CHUNKS, data->type());
    ASSERT_EQ(2u, data->graphicBlocks().size());
    EXPECT_EQ(graphicBlock1->handle(), data->graphicBlocks().front().handle());
    EXPECT_EQ(graphicBlock2->handle(), data->graphicBlocks().back().handle());
    EXPECT_TRUE(data->linearBlocks().empty());
}

void DestroyCallback(const C2Buffer * /* buf */, void *arg) {
    std::function<void(void)> *cb = (std::function<void(void)> *)arg;
    (*cb)();
}

enum : uint32_t {
    kParamIndexNumber1,
    kParamIndexNumber2,
};

typedef C2GlobalParam<C2Info, C2Int32Value, kParamIndexNumber1> C2Number1Info;
typedef C2GlobalParam<C2Info, C2Int32Value, kParamIndexNumber2> C2Number2Info;

TEST_F(C2BufferTest, BufferTest) {
    std::shared_ptr<C2BlockAllocator> alloc(makeLinearBlockAllocator());
    constexpr size_t kCapacity = 1024u;
    std::shared_ptr<C2LinearBlock> block;

    ASSERT_EQ(C2_OK, alloc->allocateLinearBlock(
            kCapacity,
            { C2MemoryUsage::kSoftwareRead, C2MemoryUsage::kSoftwareWrite },
            &block));

    std::atomic_bool destroyed(false);
    std::function<void(void)> arg = [&destroyed](){ destroyed = true; };

    std::shared_ptr<C2Buffer> buffer(new Buffer( { block->share(0, kCapacity, C2Fence()) }));
    ASSERT_EQ(C2_OK, buffer->registerOnDestroyNotify(&DestroyCallback, &arg));
    EXPECT_FALSE(destroyed);
    ASSERT_EQ(C2_DUPLICATE, buffer->registerOnDestroyNotify(&DestroyCallback, &arg));
    buffer.reset();
    EXPECT_TRUE(destroyed);

    buffer.reset(new Buffer( { block->share(0, kCapacity, C2Fence()) }));
    destroyed = false;
    ASSERT_EQ(C2_OK, buffer->registerOnDestroyNotify(&DestroyCallback, &arg));
    EXPECT_FALSE(destroyed);
    ASSERT_EQ(C2_NOT_FOUND, buffer->unregisterOnDestroyNotify(&DestroyCallback, nullptr));
    ASSERT_EQ(C2_OK, buffer->unregisterOnDestroyNotify(&DestroyCallback, &arg));
    EXPECT_FALSE(destroyed);
    ASSERT_EQ(C2_NOT_FOUND, buffer->unregisterOnDestroyNotify(&DestroyCallback, &arg));
    buffer.reset();
    EXPECT_FALSE(destroyed);

    std::shared_ptr<C2Info> info1(new C2Number1Info(1));
    std::shared_ptr<C2Info> info2(new C2Number2Info(2));
    buffer.reset(new Buffer( { block->share(0, kCapacity, C2Fence()) }));
    EXPECT_TRUE(buffer->infos().empty());
    EXPECT_FALSE(buffer->hasInfo(info1->type()));
    EXPECT_FALSE(buffer->hasInfo(info2->type()));

    ASSERT_EQ(C2_OK, buffer->setInfo(info1));
    EXPECT_EQ(1u, buffer->infos().size());
    EXPECT_EQ(*info1, *buffer->infos().front());
    EXPECT_TRUE(buffer->hasInfo(info1->type()));
    EXPECT_FALSE(buffer->hasInfo(info2->type()));

    ASSERT_EQ(C2_OK, buffer->setInfo(info2));
    EXPECT_EQ(2u, buffer->infos().size());
    EXPECT_TRUE(buffer->hasInfo(info1->type()));
    EXPECT_TRUE(buffer->hasInfo(info2->type()));

    std::shared_ptr<C2Info> removed = buffer->removeInfo(info1->type());
    ASSERT_TRUE(removed);
    EXPECT_EQ(*removed, *info1);
    EXPECT_EQ(1u, buffer->infos().size());
    EXPECT_EQ(*info2, *buffer->infos().front());
    EXPECT_FALSE(buffer->hasInfo(info1->type()));
    EXPECT_TRUE(buffer->hasInfo(info2->type()));

    removed = buffer->removeInfo(info1->type());
    ASSERT_FALSE(removed);
    EXPECT_EQ(1u, buffer->infos().size());
    EXPECT_FALSE(buffer->hasInfo(info1->type()));
    EXPECT_TRUE(buffer->hasInfo(info2->type()));

    std::shared_ptr<C2Info> info3(new C2Number2Info(3));
    ASSERT_EQ(C2_OK, buffer->setInfo(info3));
    EXPECT_EQ(1u, buffer->infos().size());
    EXPECT_FALSE(buffer->hasInfo(info1->type()));
    EXPECT_TRUE(buffer->hasInfo(info2->type()));

    removed = buffer->removeInfo(info2->type());
    ASSERT_TRUE(removed);
    EXPECT_EQ(*info3, *removed);
    EXPECT_TRUE(buffer->infos().empty());
    EXPECT_FALSE(buffer->hasInfo(info1->type()));
    EXPECT_FALSE(buffer->hasInfo(info2->type()));
}

} // namespace android
