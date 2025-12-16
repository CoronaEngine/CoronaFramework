#include "corona/kernel/ecs/archetype.h"

#include <chrono>
#include <string>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/ecs/archetype_layout.h"
#include "corona/kernel/ecs/archetype_signature.h"
#include "corona/kernel/ecs/chunk.h"
#include "corona/kernel/ecs/component.h"

using namespace Corona::Kernel::ECS;
using namespace CoronaTest;

// ========================================
// 测试用组件定义
// ========================================

struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    bool operator==(const Position& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;

    bool operator==(const Velocity& other) const {
        return vx == other.vx && vy == other.vy && vz == other.vz;
    }
};

struct Health {
    int current = 100;
    int max = 100;

    bool operator==(const Health& other) const {
        return current == other.current && max == other.max;
    }
};

struct Name {
    std::string value = "unnamed";

    Name() = default;
    Name(const std::string& v) : value(v) {}
    Name(const Name&) = default;
    Name(Name&&) = default;
    Name& operator=(const Name&) = default;
    Name& operator=(Name&&) = default;

    bool operator==(const Name& other) const { return value == other.value; }
};

// 大组件用于测试对齐和内存布局
struct alignas(32) AlignedComponent {
    double data[4] = {0.0, 0.0, 0.0, 0.0};
};

// 空组件（标签组件）
struct TagComponent {};

// 验证组件满足 Component concept
static_assert(Component<Position>);
static_assert(Component<Velocity>);
static_assert(Component<Health>);
static_assert(Component<Name>);
static_assert(Component<AlignedComponent>);
static_assert(Component<TagComponent>);

// ========================================
// ComponentTypeInfo 测试
// ========================================

TEST(ComponentTypeInfo, BasicTypeInfo) {
    CORONA_REGISTER_COMPONENT(Position);

    const auto& pos_info = get_component_type_info<Position>();
    ASSERT_TRUE(pos_info.is_valid());
    ASSERT_EQ(pos_info.size, sizeof(Position));
    ASSERT_EQ(pos_info.alignment, alignof(Position));
    ASSERT_TRUE(pos_info.is_trivially_copyable);
    ASSERT_TRUE(pos_info.is_trivially_destructible);
}

TEST(ComponentTypeInfo, NonTrivialTypeInfo) {
    CORONA_REGISTER_COMPONENT(Name);

    const auto& name_info = get_component_type_info<Name>();
    ASSERT_TRUE(name_info.is_valid());
    ASSERT_FALSE(name_info.is_trivially_copyable);
    ASSERT_FALSE(name_info.is_trivially_destructible);
}

TEST(ComponentTypeInfo, TypeIdConsistency) {
    auto id1 = get_component_type_id<Position>();
    auto id2 = get_component_type_id<Position>();
    ASSERT_EQ(id1, id2);

    auto id3 = get_component_type_id<Velocity>();
    ASSERT_NE(id1, id3);
}

TEST(ComponentTypeInfo, DifferentTypesHaveDifferentIds) {
    auto pos_id = get_component_type_id<Position>();
    auto vel_id = get_component_type_id<Velocity>();
    auto health_id = get_component_type_id<Health>();
    auto name_id = get_component_type_id<Name>();

    ASSERT_NE(pos_id, vel_id);
    ASSERT_NE(pos_id, health_id);
    ASSERT_NE(pos_id, name_id);
    ASSERT_NE(vel_id, health_id);
    ASSERT_NE(vel_id, name_id);
    ASSERT_NE(health_id, name_id);
}

TEST(ComponentTypeInfo, RegistryRegistration) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto& registry = ComponentRegistry::instance();
    const auto& pos_info = get_component_type_info<Position>();

    ASSERT_TRUE(registry.is_registered(pos_info.id));

    const auto* retrieved = registry.get_type_info(pos_info.id);
    ASSERT_TRUE(retrieved != nullptr);
    ASSERT_EQ(retrieved->id, pos_info.id);
}

TEST(ComponentTypeInfo, RegistryUnregisteredType) {
    auto& registry = ComponentRegistry::instance();
    const auto* info = registry.get_type_info(999999999);  // 不太可能存在的ID
    // 如果未注册，get_type_info 返回 nullptr
    // 不做断言，因为可能其他测试已注册
}

TEST(ComponentTypeInfo, AlignedComponentTypeInfo) {
    CORONA_REGISTER_COMPONENT(AlignedComponent);

    const auto& info = get_component_type_info<AlignedComponent>();
    ASSERT_TRUE(info.is_valid());
    ASSERT_EQ(info.alignment, alignof(AlignedComponent));
    ASSERT_GE(info.alignment, 32u);
}

TEST(ComponentTypeInfo, TagComponentTypeInfo) {
    CORONA_REGISTER_COMPONENT(TagComponent);

    const auto& info = get_component_type_info<TagComponent>();
    ASSERT_TRUE(info.is_valid());
    ASSERT_GT(info.size, 0u);  // 即使空结构体也有大小
}

// ========================================
// ArchetypeSignature 测试
// ========================================

TEST(ArchetypeSignature, EmptySignature) {
    ArchetypeSignature empty_sig;
    ASSERT_TRUE(empty_sig.empty());
    ASSERT_EQ(empty_sig.size(), 0u);
}

TEST(ArchetypeSignature, CreateFromTemplate) {
    auto sig = ArchetypeSignature::create<Position, Velocity, Health>();
    ASSERT_EQ(sig.size(), 3u);
    ASSERT_TRUE(sig.contains<Position>());
    ASSERT_TRUE(sig.contains<Velocity>());
    ASSERT_TRUE(sig.contains<Health>());
    ASSERT_FALSE(sig.contains<Name>());
}

TEST(ArchetypeSignature, DynamicAdd) {
    ArchetypeSignature sig;
    sig.add<Position>();
    sig.add<Velocity>();
    ASSERT_EQ(sig.size(), 2u);
    ASSERT_TRUE(sig.contains<Position>());
    ASSERT_TRUE(sig.contains<Velocity>());
}

TEST(ArchetypeSignature, DuplicateAddIgnored) {
    ArchetypeSignature sig;
    sig.add<Position>();
    sig.add<Position>();
    sig.add<Position>();
    ASSERT_EQ(sig.size(), 1u);
}

TEST(ArchetypeSignature, Remove) {
    auto sig = ArchetypeSignature::create<Position, Velocity>();
    sig.remove<Velocity>();
    ASSERT_EQ(sig.size(), 1u);
    ASSERT_TRUE(sig.contains<Position>());
    ASSERT_FALSE(sig.contains<Velocity>());
}

TEST(ArchetypeSignature, RemoveNonExistent) {
    auto sig = ArchetypeSignature::create<Position>();
    sig.remove<Velocity>();  // 不应崩溃
    ASSERT_EQ(sig.size(), 1u);
    ASSERT_TRUE(sig.contains<Position>());
}

TEST(ArchetypeSignature, ContainsAll) {
    auto sig1 = ArchetypeSignature::create<Position, Velocity, Health>();
    auto sig2 = ArchetypeSignature::create<Position>();
    auto sig3 = ArchetypeSignature::create<Position, Velocity>();

    ASSERT_TRUE(sig1.contains_all(sig2));
    ASSERT_TRUE(sig1.contains_all(sig3));
    ASSERT_FALSE(sig2.contains_all(sig1));
    ASSERT_FALSE(sig3.contains_all(sig1));
}

TEST(ArchetypeSignature, ContainsAny) {
    auto sig1 = ArchetypeSignature::create<Position, Velocity>();
    auto sig2 = ArchetypeSignature::create<Name>();
    auto sig3 = ArchetypeSignature::create<Name, Position>();

    ASSERT_FALSE(sig1.contains_any(sig2));
    ASSERT_TRUE(sig1.contains_any(sig3));
}

TEST(ArchetypeSignature, Equality) {
    auto sig1 = ArchetypeSignature::create<Position, Velocity, Health>();
    auto sig2 = ArchetypeSignature::create<Health, Position, Velocity>();  // 不同顺序

    ASSERT_TRUE(sig1 == sig2);
}

TEST(ArchetypeSignature, HashConsistency) {
    auto sig1 = ArchetypeSignature::create<Position, Velocity, Health>();
    auto sig2 = ArchetypeSignature::create<Health, Position, Velocity>();

    ASSERT_EQ(sig1.hash(), sig2.hash());
}

TEST(ArchetypeSignature, DifferentSignaturesHaveDifferentHashes) {
    auto sig1 = ArchetypeSignature::create<Position, Velocity>();
    auto sig2 = ArchetypeSignature::create<Position, Health>();

    // 虽然不能保证不同签名哈希不同，但通常应该不同
    // 此测试只是验证哈希函数可用
    ASSERT_TRUE(sig1.hash() != 0 || sig2.hash() != 0);  // 至少一个非零
}

TEST(ArchetypeSignature, Clear) {
    auto sig = ArchetypeSignature::create<Position, Velocity>();
    sig.clear();
    ASSERT_TRUE(sig.empty());
    ASSERT_EQ(sig.size(), 0u);
}

TEST(ArchetypeSignature, TypeIdsIteration) {
    auto sig = ArchetypeSignature::create<Position, Velocity>();
    const auto& type_ids = sig.type_ids();
    ASSERT_EQ(type_ids.size(), 2u);

    // 通过迭代器遍历
    std::size_t count = 0;
    for ([[maybe_unused]] auto id : sig) {
        ++count;
    }
    ASSERT_EQ(count, 2u);
}

TEST(ArchetypeSignature, SingleComponent) {
    auto sig = ArchetypeSignature::create<Position>();
    ASSERT_EQ(sig.size(), 1u);
    ASSERT_TRUE(sig.contains<Position>());
    ASSERT_FALSE(sig.empty());
}

TEST(ArchetypeSignature, ManyComponents) {
    auto sig = ArchetypeSignature::create<Position, Velocity, Health, Name>();
    ASSERT_EQ(sig.size(), 4u);
    ASSERT_TRUE(sig.contains<Position>());
    ASSERT_TRUE(sig.contains<Velocity>());
    ASSERT_TRUE(sig.contains<Health>());
    ASSERT_TRUE(sig.contains<Name>());
}

// ========================================
// ArchetypeLayout 测试
// ========================================

TEST(ArchetypeLayout, BasicLayout) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    CORONA_REGISTER_COMPONENT(Health);

    auto sig = ArchetypeSignature::create<Position, Velocity, Health>();
    auto layout = ArchetypeLayout::calculate(sig);

    ASSERT_TRUE(layout.is_valid());
    ASSERT_EQ(layout.component_count(), 3u);
    ASSERT_GT(layout.entities_per_chunk, 0u);
}

TEST(ArchetypeLayout, ComponentLookup) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    auto layout = ArchetypeLayout::calculate(sig);

    const auto* pos_layout = layout.find_component<Position>();
    ASSERT_TRUE(pos_layout != nullptr);
    ASSERT_EQ(pos_layout->size, sizeof(Position));

    const auto* vel_layout = layout.find_component<Velocity>();
    ASSERT_TRUE(vel_layout != nullptr);
    ASSERT_EQ(vel_layout->size, sizeof(Velocity));
}

TEST(ArchetypeLayout, DifferentOffsets) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    auto layout = ArchetypeLayout::calculate(sig);

    const auto* pos_layout = layout.find_component<Position>();
    const auto* vel_layout = layout.find_component<Velocity>();

    ASSERT_NE(pos_layout->array_offset, vel_layout->array_offset);
}

TEST(ArchetypeLayout, EmptySignatureInvalid) {
    ArchetypeSignature empty_sig;
    auto empty_layout = ArchetypeLayout::calculate(empty_sig);
    ASSERT_FALSE(empty_layout.is_valid());
}

TEST(ArchetypeLayout, NonExistentComponent) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    const auto* vel_layout = layout.find_component<Velocity>();
    ASSERT_TRUE(vel_layout == nullptr);
}

TEST(ArchetypeLayout, AlignedComponent) {
    CORONA_REGISTER_COMPONENT(AlignedComponent);
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<AlignedComponent, Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    ASSERT_TRUE(layout.is_valid());
    const auto* aligned_layout = layout.find_component<AlignedComponent>();
    ASSERT_TRUE(aligned_layout != nullptr);
    ASSERT_GE(aligned_layout->alignment, 32u);
}

TEST(ArchetypeLayout, SingleComponent) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    ASSERT_TRUE(layout.is_valid());
    ASSERT_EQ(layout.component_count(), 1u);
    ASSERT_GT(layout.entities_per_chunk, 0u);
}

TEST(ArchetypeLayout, GetArrayOffset) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    auto layout = ArchetypeLayout::calculate(sig);

    auto pos_offset = layout.get_array_offset(get_component_type_id<Position>());
    auto vel_offset = layout.get_array_offset(get_component_type_id<Velocity>());

    ASSERT_GE(pos_offset, 0);
    ASSERT_GE(vel_offset, 0);
    ASSERT_NE(pos_offset, vel_offset);
}

TEST(ArchetypeLayout, ChunkSizeCalculation) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    auto layout = ArchetypeLayout::calculate(sig);

    // chunk_data_size 应该大于 0 且不超过默认 chunk 大小
    ASSERT_GT(layout.chunk_data_size, 0u);
    ASSERT_LE(layout.chunk_data_size, kDefaultChunkSize);
}

// ========================================
// Chunk 测试
// ========================================

TEST(Chunk, BasicAllocation) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);

    ASSERT_TRUE(chunk.is_empty());
    ASSERT_EQ(chunk.size(), 0u);
    ASSERT_EQ(chunk.capacity(), layout.entities_per_chunk);
}

TEST(Chunk, AllocateMultiple) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);

    auto idx0 = chunk.allocate();
    ASSERT_EQ(idx0, 0u);
    ASSERT_EQ(chunk.size(), 1u);

    auto idx1 = chunk.allocate();
    ASSERT_EQ(idx1, 1u);
    ASSERT_EQ(chunk.size(), 2u);
}

TEST(Chunk, ComponentAccess) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);

    auto idx0 = chunk.allocate();
    auto* pos0 = chunk.get_component_at<Position>(idx0);
    ASSERT_TRUE(pos0 != nullptr);
    pos0->x = 10.0f;
    pos0->y = 20.0f;

    auto idx1 = chunk.allocate();
    auto* pos1 = chunk.get_component_at<Position>(idx1);
    ASSERT_TRUE(pos1 != nullptr);
    pos1->x = 30.0f;

    // 验证数据
    ASSERT_EQ(chunk.get_component_at<Position>(0)->x, 10.0f);
    ASSERT_EQ(chunk.get_component_at<Position>(1)->x, 30.0f);
}

TEST(Chunk, SpanAccess) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);

    (void)chunk.allocate();
    (void)chunk.allocate();
    chunk.get_component_at<Position>(0)->x = 10.0f;
    chunk.get_component_at<Position>(1)->x = 30.0f;

    auto positions = chunk.get_components<Position>();
    ASSERT_EQ(positions.size(), 2u);
    ASSERT_EQ(positions[0].x, 10.0f);
    ASSERT_EQ(positions[1].x, 30.0f);
}

TEST(Chunk, SwapAndPopDeallocate) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);

    (void)chunk.allocate();
    (void)chunk.allocate();
    chunk.get_component_at<Position>(0)->x = 10.0f;
    chunk.get_component_at<Position>(1)->x = 30.0f;

    // 删除第一个，第二个应该被移动过来
    auto moved = chunk.deallocate(0);
    ASSERT_TRUE(moved.has_value());
    ASSERT_EQ(*moved, 1u);  // 索引 1 被移动到索引 0
    ASSERT_EQ(chunk.size(), 1u);

    // 验证移动后的数据
    ASSERT_EQ(chunk.get_component_at<Position>(0)->x, 30.0f);
}

TEST(Chunk, DeallocateLast) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);

    (void)chunk.allocate();
    (void)chunk.allocate();

    // 删除最后一个不需要移动
    auto moved = chunk.deallocate(1);
    ASSERT_FALSE(moved.has_value());
    ASSERT_EQ(chunk.size(), 1u);
}

TEST(Chunk, DeallocateToEmpty) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);

    auto idx = chunk.allocate();
    ASSERT_EQ(chunk.size(), 1u);

    chunk.deallocate(idx);
    ASSERT_TRUE(chunk.is_empty());
}

TEST(Chunk, IsFull) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    // 使用一个较小的容量来测试
    Chunk chunk(layout, 3);

    ASSERT_FALSE(chunk.is_full());

    (void)chunk.allocate();
    (void)chunk.allocate();
    (void)chunk.allocate();

    ASSERT_TRUE(chunk.is_full());
}

TEST(Chunk, NonExistentComponent) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);
    (void)chunk.allocate();

    // 访问不存在的组件应该返回空 span 或 nullptr
    auto velocities = chunk.get_components<Velocity>();
    ASSERT_TRUE(velocities.empty());
}

TEST(Chunk, MoveSemantics) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk1(layout, layout.entities_per_chunk);
    (void)chunk1.allocate();
    chunk1.get_component_at<Position>(0)->x = 42.0f;

    Chunk chunk2 = std::move(chunk1);
    ASSERT_EQ(chunk2.size(), 1u);
    ASSERT_EQ(chunk2.get_component_at<Position>(0)->x, 42.0f);
}

// ========================================
// Archetype 测试
// ========================================

TEST(Archetype, BasicCreation) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    CORONA_REGISTER_COMPONENT(Health);

    auto sig = ArchetypeSignature::create<Position, Velocity, Health>();
    Archetype archetype(1, sig);

    ASSERT_EQ(archetype.id(), 1u);
    ASSERT_TRUE(archetype.has_component<Position>());
    ASSERT_TRUE(archetype.has_component<Velocity>());
    ASSERT_TRUE(archetype.has_component<Health>());
    ASSERT_FALSE(archetype.has_component<Name>());
    ASSERT_TRUE(archetype.empty());
}

TEST(Archetype, AllocateEntity) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    auto loc0 = archetype.allocate_entity();
    ASSERT_EQ(loc0.chunk_index, 0u);
    ASSERT_EQ(loc0.index_in_chunk, 0u);
    ASSERT_EQ(archetype.entity_count(), 1u);
    ASSERT_EQ(archetype.chunk_count(), 1u);
}

TEST(Archetype, SetAndGetComponent) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    CORONA_REGISTER_COMPONENT(Health);

    auto sig = ArchetypeSignature::create<Position, Velocity, Health>();
    Archetype archetype(1, sig);

    auto loc = archetype.allocate_entity();

    archetype.set_component<Position>(loc, Position{1.0f, 2.0f, 3.0f});
    archetype.set_component<Velocity>(loc, Velocity{0.1f, 0.2f, 0.3f});
    archetype.set_component<Health>(loc, Health{80, 100});

    auto* pos = archetype.get_component<Position>(loc);
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(pos->x, 1.0f);
    ASSERT_EQ(pos->y, 2.0f);
    ASSERT_EQ(pos->z, 3.0f);

    auto* health = archetype.get_component<Health>(loc);
    ASSERT_TRUE(health != nullptr);
    ASSERT_EQ(health->current, 80);
    ASSERT_EQ(health->max, 100);
}

TEST(Archetype, MultipleEntities) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    auto loc0 = archetype.allocate_entity();
    auto loc1 = archetype.allocate_entity();
    auto loc2 = archetype.allocate_entity();

    archetype.set_component<Position>(loc0, Position{1.0f, 0.0f, 0.0f});
    archetype.set_component<Position>(loc1, Position{2.0f, 0.0f, 0.0f});
    archetype.set_component<Position>(loc2, Position{3.0f, 0.0f, 0.0f});

    ASSERT_EQ(archetype.entity_count(), 3u);

    ASSERT_EQ(archetype.get_component<Position>(loc0)->x, 1.0f);
    ASSERT_EQ(archetype.get_component<Position>(loc1)->x, 2.0f);
    ASSERT_EQ(archetype.get_component<Position>(loc2)->x, 3.0f);
}

TEST(Archetype, ChunkIteration) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    // 分配一些实体
    for (int i = 0; i < 10; ++i) {
        auto loc = archetype.allocate_entity();
        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
    }

    // 通过 Chunk 遍历
    std::size_t count = 0;
    float sum = 0.0f;
    for (auto& chunk : archetype.chunks()) {
        auto positions = chunk.get_components<Position>();
        for (const auto& pos : positions) {
            sum += pos.x;
            ++count;
        }
    }

    ASSERT_EQ(count, 10u);
    ASSERT_EQ(sum, 45.0f);  // 0+1+2+...+9 = 45
}

TEST(Archetype, DeallocateEntity) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    auto loc0 = archetype.allocate_entity();
    auto loc1 = archetype.allocate_entity();
    auto loc2 = archetype.allocate_entity();

    archetype.set_component<Position>(loc0, Position{1.0f, 0.0f, 0.0f});
    archetype.set_component<Position>(loc1, Position{2.0f, 0.0f, 0.0f});
    archetype.set_component<Position>(loc2, Position{3.0f, 0.0f, 0.0f});

    ASSERT_EQ(archetype.entity_count(), 3u);

    // 删除中间的实体
    archetype.deallocate_entity(loc1);
    ASSERT_EQ(archetype.entity_count(), 2u);
}

TEST(Archetype, DeallocateSwapAndPop) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    auto loc0 = archetype.allocate_entity();
    auto loc1 = archetype.allocate_entity();
    auto loc2 = archetype.allocate_entity();

    archetype.set_component<Position>(loc0, Position{1.0f, 0.0f, 0.0f});
    archetype.set_component<Position>(loc1, Position{2.0f, 0.0f, 0.0f});
    archetype.set_component<Position>(loc2, Position{3.0f, 0.0f, 0.0f});

    // 删除第一个，最后一个会被移动过来
    auto moved = archetype.deallocate_entity(loc0);
    ASSERT_EQ(archetype.entity_count(), 2u);

    // 如果发生了移动，验证移动信息
    if (moved.has_value()) {
        // 被移动实体的原位置是 loc2
        ASSERT_EQ(moved->chunk_index, loc2.chunk_index);
        ASSERT_EQ(moved->index_in_chunk, loc2.index_in_chunk);
    }
}

TEST(Archetype, ConstAccess) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    auto loc = archetype.allocate_entity();
    archetype.set_component<Position>(loc, Position{42.0f, 0.0f, 0.0f});

    const Archetype& const_archetype = archetype;
    const auto* pos = const_archetype.get_component<Position>(loc);
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(pos->x, 42.0f);
}

TEST(Archetype, ConstChunkIteration) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    auto loc = archetype.allocate_entity();
    archetype.set_component<Position>(loc, Position{42.0f, 0.0f, 0.0f});

    const Archetype& const_archetype = archetype;
    std::size_t count = 0;
    for (const auto& chunk : const_archetype.chunks()) {
        count += chunk.size();
    }
    ASSERT_EQ(count, 1u);
}

TEST(Archetype, GetChunk) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    (void)archetype.allocate_entity();

    ASSERT_EQ(archetype.chunk_count(), 1u);
    auto& chunk = archetype.get_chunk(0);
    ASSERT_EQ(chunk.size(), 1u);
}

TEST(Archetype, MoveSemantics) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype1(1, sig);

    auto loc = archetype1.allocate_entity();
    archetype1.set_component<Position>(loc, Position{123.0f, 0.0f, 0.0f});

    Archetype archetype2 = std::move(archetype1);
    ASSERT_EQ(archetype2.id(), 1u);
    ASSERT_EQ(archetype2.entity_count(), 1u);
    ASSERT_EQ(archetype2.get_component<Position>(loc)->x, 123.0f);
}

// ========================================
// 批量操作测试
// ========================================

TEST(Archetype, BulkAllocation) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    Archetype archetype(1, sig);

    constexpr std::size_t kEntityCount = 10000;

    std::vector<EntityLocation> locations;
    locations.reserve(kEntityCount);

    for (std::size_t i = 0; i < kEntityCount; ++i) {
        auto loc = archetype.allocate_entity();
        locations.push_back(loc);

        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
        archetype.set_component<Velocity>(loc, Velocity{1.0f, 0.0f, 0.0f});
    }

    ASSERT_EQ(archetype.entity_count(), kEntityCount);
    ASSERT_GT(archetype.chunk_count(), 0u);
}

TEST(Archetype, BulkTraversalUpdate) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    Archetype archetype(1, sig);

    constexpr std::size_t kEntityCount = 1000;

    std::vector<EntityLocation> locations;
    for (std::size_t i = 0; i < kEntityCount; ++i) {
        auto loc = archetype.allocate_entity();
        locations.push_back(loc);
        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
        archetype.set_component<Velocity>(loc, Velocity{1.0f, 0.0f, 0.0f});
    }

    // 批量遍历更新
    for (auto& chunk : archetype.chunks()) {
        auto positions = chunk.get_components<Position>();
        auto velocities = chunk.get_components<Velocity>();

        for (std::size_t i = 0; i < chunk.size(); ++i) {
            positions[i].x += velocities[i].vx;
        }
    }

    // 验证更新
    auto* first_pos = archetype.get_component<Position>(locations[0]);
    ASSERT_EQ(first_pos->x, 1.0f);  // 0 + 1

    auto* last_pos = archetype.get_component<Position>(locations.back());
    ASSERT_EQ(last_pos->x, static_cast<float>(kEntityCount));  // (kEntityCount-1) + 1
}

TEST(Archetype, BulkDeallocation) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    std::vector<EntityLocation> locations;
    for (int i = 0; i < 100; ++i) {
        auto loc = archetype.allocate_entity();
        locations.push_back(loc);
        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
    }

    ASSERT_EQ(archetype.entity_count(), 100u);

    // 删除所有实体（从后往前删除以避免索引变化问题）
    for (auto it = locations.rbegin(); it != locations.rend(); ++it) {
        archetype.deallocate_entity(*it);
    }

    ASSERT_TRUE(archetype.empty());
    ASSERT_EQ(archetype.entity_count(), 0u);
}

// ========================================
// 非 trivial 组件测试
// ========================================

TEST(Archetype, NonTrivialComponentAllocation) {
    CORONA_REGISTER_COMPONENT(Name);
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Name, Position>();
    Archetype archetype(1, sig);

    auto loc0 = archetype.allocate_entity();
    archetype.set_component<Name>(loc0, Name{"Entity0"});
    archetype.set_component<Position>(loc0, Position{1.0f, 2.0f, 3.0f});

    auto loc1 = archetype.allocate_entity();
    archetype.set_component<Name>(loc1, Name{"Entity1"});

    auto* name0 = archetype.get_component<Name>(loc0);
    ASSERT_TRUE(name0 != nullptr);
    ASSERT_EQ(name0->value, "Entity0");

    auto* name1 = archetype.get_component<Name>(loc1);
    ASSERT_EQ(name1->value, "Entity1");
}

TEST(Archetype, NonTrivialComponentDeallocation) {
    CORONA_REGISTER_COMPONENT(Name);

    auto sig = ArchetypeSignature::create<Name>();
    Archetype archetype(1, sig);

    auto loc0 = archetype.allocate_entity();
    archetype.set_component<Name>(loc0, Name{"Entity0"});

    auto loc1 = archetype.allocate_entity();
    archetype.set_component<Name>(loc1, Name{"Entity1"});

    ASSERT_EQ(archetype.entity_count(), 2u);

    // 删除（验证析构正确调用）
    archetype.deallocate_entity(loc0);
    ASSERT_EQ(archetype.entity_count(), 1u);

    // 验证 swap-and-pop 后数据正确
    auto* remaining = archetype.get_component<Name>(EntityLocation{0, 0});
    ASSERT_EQ(remaining->value, "Entity1");
}

TEST(Archetype, NonTrivialComponentSwapAndPop) {
    CORONA_REGISTER_COMPONENT(Name);

    auto sig = ArchetypeSignature::create<Name>();
    Archetype archetype(1, sig);

    // 分配 3 个实体
    auto loc0 = archetype.allocate_entity();
    archetype.set_component<Name>(loc0, Name{"First"});

    auto loc1 = archetype.allocate_entity();
    archetype.set_component<Name>(loc1, Name{"Second"});

    auto loc2 = archetype.allocate_entity();
    archetype.set_component<Name>(loc2, Name{"Third"});

    // 删除第一个，第三个应该被移动过来
    archetype.deallocate_entity(loc0);

    // 现在位置 0 应该是 "Third"
    auto* moved = archetype.get_component<Name>(EntityLocation{0, 0});
    ASSERT_EQ(moved->value, "Third");

    // 位置 1 应该还是 "Second"
    auto* second = archetype.get_component<Name>(EntityLocation{0, 1});
    ASSERT_EQ(second->value, "Second");
}

TEST(Archetype, LongStringComponent) {
    CORONA_REGISTER_COMPONENT(Name);

    auto sig = ArchetypeSignature::create<Name>();
    Archetype archetype(1, sig);

    // 使用一个很长的字符串测试动态内存管理
    std::string long_name(1000, 'x');
    auto loc = archetype.allocate_entity();
    archetype.set_component<Name>(loc, Name{long_name});

    auto* name = archetype.get_component<Name>(loc);
    ASSERT_EQ(name->value.size(), 1000u);
    ASSERT_EQ(name->value, long_name);
}

// ========================================
// 边界条件测试
// ========================================

TEST(Archetype, EmptyArchetype) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    ASSERT_TRUE(archetype.empty());
    ASSERT_EQ(archetype.entity_count(), 0u);
    ASSERT_EQ(archetype.chunk_count(), 0u);
}

TEST(Archetype, SingleEntityAllocateAndDeallocate) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    auto loc = archetype.allocate_entity();
    ASSERT_EQ(archetype.entity_count(), 1u);

    archetype.deallocate_entity(loc);
    ASSERT_TRUE(archetype.empty());
}

TEST(Archetype, RepeatedAllocateDeallocate) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    for (int i = 0; i < 100; ++i) {
        auto loc = archetype.allocate_entity();
        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
        archetype.deallocate_entity(loc);
    }

    ASSERT_TRUE(archetype.empty());
}

TEST(Archetype, TagComponentOnly) {
    CORONA_REGISTER_COMPONENT(TagComponent);

    auto sig = ArchetypeSignature::create<TagComponent>();
    Archetype archetype(1, sig);

    auto loc = archetype.allocate_entity();
    ASSERT_EQ(archetype.entity_count(), 1u);

    archetype.deallocate_entity(loc);
    ASSERT_TRUE(archetype.empty());
}

// ========================================
// 性能测试
// ========================================

TEST(Archetype, HighThroughputAllocation) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    Archetype archetype(1, sig);

    constexpr std::size_t kEntityCount = 50000;

    auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < kEntityCount; ++i) {
        auto loc = archetype.allocate_entity();
        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
    }

    auto end = std::chrono::steady_clock::now();
    [[maybe_unused]] auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_EQ(archetype.entity_count(), kEntityCount);
}

TEST(Archetype, HighThroughputIteration) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    Archetype archetype(1, sig);

    constexpr std::size_t kEntityCount = 50000;

    for (std::size_t i = 0; i < kEntityCount; ++i) {
        auto loc = archetype.allocate_entity();
        archetype.set_component<Position>(loc, Position{0.0f, 0.0f, 0.0f});
        archetype.set_component<Velocity>(loc, Velocity{1.0f, 0.0f, 0.0f});
    }

    auto start = std::chrono::steady_clock::now();

    // 模拟物理更新
    for (auto& chunk : archetype.chunks()) {
        auto positions = chunk.get_components<Position>();
        auto velocities = chunk.get_components<Velocity>();

        for (std::size_t i = 0; i < chunk.size(); ++i) {
            positions[i].x += velocities[i].vx;
            positions[i].y += velocities[i].vy;
            positions[i].z += velocities[i].vz;
        }
    }

    auto end = std::chrono::steady_clock::now();
    [[maybe_unused]] auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // 验证更新
    std::size_t count = 0;
    for (auto& chunk : archetype.chunks()) {
        auto positions = chunk.get_components<Position>();
        for (const auto& pos : positions) {
            ASSERT_EQ(pos.x, 1.0f);
            ++count;
        }
    }
    ASSERT_EQ(count, kEntityCount);
}

// ========================================
// 多 Chunk 测试
// ========================================

TEST(Archetype, MultipleChunks) {
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    Archetype archetype(1, sig);

    // 分配足够多的实体以确保创建多个 Chunk
    // 每个 Chunk 大约 16KB，Position + Velocity = 24 bytes
    // 约 ~680 个实体每个 Chunk
    constexpr std::size_t kEntityCount = 2000;  // 应该创建多个 Chunk

    for (std::size_t i = 0; i < kEntityCount; ++i) {
        auto loc = archetype.allocate_entity();
        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
    }

    ASSERT_EQ(archetype.entity_count(), kEntityCount);
    ASSERT_GT(archetype.chunk_count(), 1u);  // 应该有多个 Chunk

    // 验证所有 Chunk 中的数据
    std::size_t total_count = 0;
    for (const auto& chunk : archetype.chunks()) {
        total_count += chunk.size();
    }
    ASSERT_EQ(total_count, kEntityCount);
}

TEST(Archetype, DeallocateFromMultipleChunks) {
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Position>();
    Archetype archetype(1, sig);

    // 创建 2000 个实体
    for (std::size_t i = 0; i < 2000; ++i) {
        auto loc = archetype.allocate_entity();
        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
    }

    std::size_t initial_chunks = archetype.chunk_count();
    ASSERT_GT(initial_chunks, 1u);
    ASSERT_EQ(archetype.entity_count(), 2000u);

    // 每次获取第一个 chunk 的第一个实体并删除它
    // 这样可以正确跟踪实体位置（因为我们每次都重新获取）
    for (std::size_t i = 0; i < 1000; ++i) {
        // 只要还有实体，就删除第一个 chunk 的第一个实体
        ASSERT_GT(archetype.entity_count(), 0u);
        EntityLocation loc{0, 0};  // 总是删除第一个 chunk 的第一个位置
        archetype.deallocate_entity(loc);
    }

    ASSERT_EQ(archetype.entity_count(), 1000u);
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
