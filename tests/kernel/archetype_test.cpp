#include "corona/kernel/ecs/archetype.h"

#include <iostream>
#include <string>

#include "../test_framework.h"
#include "corona/kernel/ecs/archetype_layout.h"
#include "corona/kernel/ecs/archetype_signature.h"
#include "corona/kernel/ecs/chunk.h"
#include "corona/kernel/ecs/component.h"

using namespace Corona::Kernel::ECS;

// ========================================
// 测试用组件定义
// ========================================

struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

struct Health {
    int current = 100;
    int max = 100;
};

struct Name {
    std::string value = "unnamed";

    Name() = default;
    Name(const std::string& v) : value(v) {}
    Name(const Name&) = default;
    Name(Name&&) = default;
    Name& operator=(const Name&) = default;
    Name& operator=(Name&&) = default;
};

// 验证组件满足 Component concept
static_assert(Component<Position>);
static_assert(Component<Velocity>);
static_assert(Component<Health>);
static_assert(Component<Name>);

// ========================================
// ComponentTypeInfo 测试
// ========================================

void test_component_type_info() {
    std::cout << "=== test_component_type_info ===" << std::endl;

    // 注册组件
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    CORONA_REGISTER_COMPONENT(Health);
    CORONA_REGISTER_COMPONENT(Name);

    // 测试 Position 类型信息
    const auto& pos_info = get_component_type_info<Position>();
    ASSERT_TRUE(pos_info.is_valid());
    ASSERT_EQ(pos_info.size, sizeof(Position));
    ASSERT_EQ(pos_info.alignment, alignof(Position));
    ASSERT_TRUE(pos_info.is_trivially_copyable);
    ASSERT_TRUE(pos_info.is_trivially_destructible);
    std::cout << "Position type name: " << pos_info.name << std::endl;

    // 测试 Name 类型信息（非 trivially copyable）
    const auto& name_info = get_component_type_info<Name>();
    ASSERT_TRUE(name_info.is_valid());
    ASSERT_FALSE(name_info.is_trivially_copyable);
    ASSERT_FALSE(name_info.is_trivially_destructible);
    std::cout << "Name type name: " << name_info.name << std::endl;

    // 测试类型 ID 一致性
    auto id1 = get_component_type_id<Position>();
    auto id2 = get_component_type_id<Position>();
    ASSERT_EQ(id1, id2);

    auto id3 = get_component_type_id<Velocity>();
    ASSERT_NE(id1, id3);

    // 测试 ComponentRegistry
    auto& registry = ComponentRegistry::instance();
    ASSERT_TRUE(registry.is_registered(pos_info.id));
    ASSERT_TRUE(registry.is_registered(name_info.id));

    const auto* retrieved = registry.get_type_info(pos_info.id);
    ASSERT_TRUE(retrieved != nullptr);
    ASSERT_EQ(retrieved->id, pos_info.id);

    std::cout << "test_component_type_info PASSED" << std::endl;
}

// ========================================
// ArchetypeSignature 测试
// ========================================

void test_archetype_signature() {
    std::cout << "=== test_archetype_signature ===" << std::endl;

    // 测试创建空签名
    ArchetypeSignature empty_sig;
    ASSERT_TRUE(empty_sig.empty());
    ASSERT_EQ(empty_sig.size(), 0);

    // 测试模板创建
    auto sig1 = ArchetypeSignature::create<Position, Velocity, Health>();
    ASSERT_EQ(sig1.size(), 3);
    ASSERT_TRUE(sig1.contains<Position>());
    ASSERT_TRUE(sig1.contains<Velocity>());
    ASSERT_TRUE(sig1.contains<Health>());
    ASSERT_FALSE(sig1.contains<Name>());

    // 测试动态添加
    ArchetypeSignature sig2;
    sig2.add<Position>();
    sig2.add<Velocity>();
    ASSERT_EQ(sig2.size(), 2);

    // 测试重复添加（不应增加）
    sig2.add<Position>();
    ASSERT_EQ(sig2.size(), 2);

    // 测试移除
    sig2.remove<Velocity>();
    ASSERT_EQ(sig2.size(), 1);
    ASSERT_FALSE(sig2.contains<Velocity>());

    // 测试包含关系
    auto sig3 = ArchetypeSignature::create<Position>();
    ASSERT_TRUE(sig1.contains_all(sig3));
    ASSERT_FALSE(sig3.contains_all(sig1));

    // 测试相等性
    auto sig4 = ArchetypeSignature::create<Health, Position, Velocity>();  // 不同顺序
    ASSERT_TRUE(sig1 == sig4);                                             // 应该相等（内部已排序）

    // 测试哈希一致性
    ASSERT_EQ(sig1.hash(), sig4.hash());

    // 测试 contains_any
    auto sig5 = ArchetypeSignature::create<Name>();
    ASSERT_FALSE(sig1.contains_any(sig5));
    sig5.add<Position>();
    ASSERT_TRUE(sig1.contains_any(sig5));

    std::cout << "test_archetype_signature PASSED" << std::endl;
}

// ========================================
// ArchetypeLayout 测试
// ========================================

void test_archetype_layout() {
    std::cout << "=== test_archetype_layout ===" << std::endl;

    // 注册组件（确保已注册）
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    CORONA_REGISTER_COMPONENT(Health);

    auto sig = ArchetypeSignature::create<Position, Velocity, Health>();
    auto layout = ArchetypeLayout::calculate(sig);

    ASSERT_TRUE(layout.is_valid());
    ASSERT_EQ(layout.component_count(), 3);
    ASSERT_TRUE(layout.entities_per_chunk > 0);

    std::cout << "Entities per chunk: " << layout.entities_per_chunk << std::endl;
    std::cout << "Total size per entity: " << layout.total_size_per_entity << std::endl;
    std::cout << "Chunk data size: " << layout.chunk_data_size << std::endl;

    // 测试组件查找
    const auto* pos_layout = layout.find_component<Position>();
    ASSERT_TRUE(pos_layout != nullptr);
    ASSERT_EQ(pos_layout->size, sizeof(Position));

    const auto* vel_layout = layout.find_component<Velocity>();
    ASSERT_TRUE(vel_layout != nullptr);

    // 验证不同组件有不同的偏移
    ASSERT_NE(pos_layout->array_offset, vel_layout->array_offset);

    // 测试空签名
    ArchetypeSignature empty_sig;
    auto empty_layout = ArchetypeLayout::calculate(empty_sig);
    ASSERT_FALSE(empty_layout.is_valid());

    std::cout << "test_archetype_layout PASSED" << std::endl;
}

// ========================================
// Chunk 测试
// ========================================

void test_chunk() {
    std::cout << "=== test_chunk ===" << std::endl;

    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    auto layout = ArchetypeLayout::calculate(sig);

    Chunk chunk(layout, layout.entities_per_chunk);

    ASSERT_TRUE(chunk.is_empty());
    ASSERT_EQ(chunk.size(), 0);
    ASSERT_EQ(chunk.capacity(), layout.entities_per_chunk);

    // 分配实体
    auto idx0 = chunk.allocate();
    ASSERT_EQ(idx0, 0);
    ASSERT_EQ(chunk.size(), 1);

    auto idx1 = chunk.allocate();
    ASSERT_EQ(idx1, 1);
    ASSERT_EQ(chunk.size(), 2);

    // 测试组件访问
    auto* pos0 = chunk.get_component_at<Position>(0);
    ASSERT_TRUE(pos0 != nullptr);
    pos0->x = 10.0f;
    pos0->y = 20.0f;

    auto* pos1 = chunk.get_component_at<Position>(1);
    ASSERT_TRUE(pos1 != nullptr);
    pos1->x = 30.0f;

    // 验证数据
    ASSERT_EQ(chunk.get_component_at<Position>(0)->x, 10.0f);
    ASSERT_EQ(chunk.get_component_at<Position>(1)->x, 30.0f);

    // 测试 span 访问
    auto positions = chunk.get_components<Position>();
    ASSERT_EQ(positions.size(), 2);
    ASSERT_EQ(positions[0].x, 10.0f);
    ASSERT_EQ(positions[1].x, 30.0f);

    // 测试 swap-and-pop 删除
    auto moved = chunk.deallocate(0);
    ASSERT_TRUE(moved.has_value());
    ASSERT_EQ(*moved, 1);  // 索引 1 被移动到索引 0
    ASSERT_EQ(chunk.size(), 1);

    // 验证移动后的数据
    ASSERT_EQ(chunk.get_component_at<Position>(0)->x, 30.0f);

    // 删除最后一个
    auto moved2 = chunk.deallocate(0);
    ASSERT_FALSE(moved2.has_value());  // 删除最后一个不需要移动
    ASSERT_EQ(chunk.size(), 0);
    ASSERT_TRUE(chunk.is_empty());

    std::cout << "test_chunk PASSED" << std::endl;
}

// ========================================
// Archetype 测试
// ========================================

void test_archetype() {
    std::cout << "=== test_archetype ===" << std::endl;

    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    CORONA_REGISTER_COMPONENT(Health);

    auto sig = ArchetypeSignature::create<Position, Velocity, Health>();
    Archetype archetype(1, sig);

    ASSERT_EQ(archetype.id(), 1);
    ASSERT_TRUE(archetype.has_component<Position>());
    ASSERT_TRUE(archetype.has_component<Velocity>());
    ASSERT_TRUE(archetype.has_component<Health>());
    ASSERT_FALSE(archetype.has_component<Name>());
    ASSERT_TRUE(archetype.empty());

    // 分配实体
    auto loc0 = archetype.allocate_entity();
    ASSERT_EQ(loc0.chunk_index, 0);
    ASSERT_EQ(loc0.index_in_chunk, 0);
    ASSERT_EQ(archetype.entity_count(), 1);
    ASSERT_EQ(archetype.chunk_count(), 1);

    // 设置组件
    archetype.set_component<Position>(loc0, Position{1.0f, 2.0f, 3.0f});
    archetype.set_component<Velocity>(loc0, Velocity{0.1f, 0.2f, 0.3f});
    archetype.set_component<Health>(loc0, Health{80, 100});

    // 获取组件
    auto* pos = archetype.get_component<Position>(loc0);
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(pos->x, 1.0f);
    ASSERT_EQ(pos->y, 2.0f);
    ASSERT_EQ(pos->z, 3.0f);

    auto* health = archetype.get_component<Health>(loc0);
    ASSERT_TRUE(health != nullptr);
    ASSERT_EQ(health->current, 80);

    // 分配更多实体
    auto loc1 = archetype.allocate_entity();
    auto loc2 = archetype.allocate_entity();
    archetype.set_component<Position>(loc1, Position{10.0f, 20.0f, 30.0f});
    archetype.set_component<Position>(loc2, Position{100.0f, 200.0f, 300.0f});

    ASSERT_EQ(archetype.entity_count(), 3);

    // 测试 Chunk 遍历
    std::size_t count = 0;
    for (auto& chunk : archetype.chunks()) {
        auto positions = chunk.get_components<Position>();
        for (const auto& p : positions) {
            std::cout << "Entity position: (" << p.x << ", " << p.y << ", " << p.z << ")"
                      << std::endl;
            ++count;
        }
    }
    ASSERT_EQ(count, 3);

    // 测试删除
    auto moved = archetype.deallocate_entity(loc0);
    ASSERT_EQ(archetype.entity_count(), 2);

    // 验证 swap-and-pop
    if (moved.has_value()) {
        std::cout << "Entity moved from chunk " << moved->chunk_index << " index "
                  << moved->index_in_chunk << std::endl;
    }

    std::cout << "test_archetype PASSED" << std::endl;
}

// ========================================
// 批量操作性能测试
// ========================================

void test_bulk_operations() {
    std::cout << "=== test_bulk_operations ===" << std::endl;

    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);

    auto sig = ArchetypeSignature::create<Position, Velocity>();
    Archetype archetype(1, sig);

    constexpr std::size_t kEntityCount = 10000;

    // 批量分配
    std::vector<EntityLocation> locations;
    locations.reserve(kEntityCount);

    for (std::size_t i = 0; i < kEntityCount; ++i) {
        auto loc = archetype.allocate_entity();
        locations.push_back(loc);

        archetype.set_component<Position>(loc, Position{static_cast<float>(i), 0.0f, 0.0f});
        archetype.set_component<Velocity>(loc, Velocity{1.0f, 0.0f, 0.0f});
    }

    ASSERT_EQ(archetype.entity_count(), kEntityCount);
    std::cout << "Allocated " << kEntityCount << " entities in " << archetype.chunk_count()
              << " chunks" << std::endl;

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

    std::cout << "test_bulk_operations PASSED" << std::endl;
}

// ========================================
// 非 trivial 组件测试
// ========================================

void test_non_trivial_components() {
    std::cout << "=== test_non_trivial_components ===" << std::endl;

    CORONA_REGISTER_COMPONENT(Name);
    CORONA_REGISTER_COMPONENT(Position);

    auto sig = ArchetypeSignature::create<Name, Position>();
    Archetype archetype(1, sig);

    // 分配带有 std::string 的实体
    auto loc0 = archetype.allocate_entity();
    archetype.set_component<Name>(loc0, Name{"Entity0"});
    archetype.set_component<Position>(loc0, Position{1.0f, 2.0f, 3.0f});

    auto loc1 = archetype.allocate_entity();
    archetype.set_component<Name>(loc1, Name{"Entity1"});

    // 验证
    auto* name0 = archetype.get_component<Name>(loc0);
    ASSERT_TRUE(name0 != nullptr);
    ASSERT_EQ(name0->value, "Entity0");

    auto* name1 = archetype.get_component<Name>(loc1);
    ASSERT_EQ(name1->value, "Entity1");

    // 测试删除（验证析构正确调用）
    archetype.deallocate_entity(loc0);
    ASSERT_EQ(archetype.entity_count(), 1);

    // 验证 swap-and-pop 后数据正确
    // loc1 应该被移动到 loc0 的位置
    auto* remaining = archetype.get_component<Name>(EntityLocation{0, 0});
    ASSERT_EQ(remaining->value, "Entity1");

    std::cout << "test_non_trivial_components PASSED" << std::endl;
}

// ========================================
// Main
// ========================================

int main() {
    try {
        test_component_type_info();
        test_archetype_signature();
        test_archetype_layout();
        test_chunk();
        test_archetype();
        test_bulk_operations();
        test_non_trivial_components();

        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "All archetype tests PASSED!" << std::endl;
        std::cout << "========================================" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test FAILED: " << e.what() << std::endl;
        return 1;
    }
}
