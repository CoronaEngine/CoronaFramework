#include "corona/kernel/ecs/world.h"

#include <string>
#include <vector>

#include "../test_framework.h"

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
    explicit Name(const std::string& v) : value(v) {}
    Name(const Name&) = default;
    Name(Name&&) = default;
    Name& operator=(const Name&) = default;
    Name& operator=(Name&&) = default;

    bool operator==(const Name& other) const { return value == other.value; }
};

// 标签组件
struct EnemyTag {};
struct PlayerTag {};

// ========================================
// World 基本测试
// ========================================

TEST(World, DefaultConstruction) {
    World world;
    ASSERT_EQ(world.entity_count(), 0u);
    ASSERT_EQ(world.archetype_count(), 0u);
}

TEST(World, CreateEmptyEntity) {
    World world;

    EntityId entity = world.create_entity();
    ASSERT_TRUE(entity.is_valid());
    ASSERT_TRUE(world.is_alive(entity));
    ASSERT_EQ(world.entity_count(), 1u);
}

TEST(World, CreateMultipleEmptyEntities) {
    World world;

    std::vector<EntityId> entities;
    for (int i = 0; i < 100; ++i) {
        entities.push_back(world.create_entity());
    }

    ASSERT_EQ(world.entity_count(), 100u);

    for (const auto& e : entities) {
        ASSERT_TRUE(world.is_alive(e));
    }
}

TEST(World, DestroyEntity) {
    World world;

    EntityId entity = world.create_entity();
    ASSERT_TRUE(world.is_alive(entity));

    ASSERT_TRUE(world.destroy_entity(entity));
    ASSERT_FALSE(world.is_alive(entity));
    ASSERT_EQ(world.entity_count(), 0u);
}

TEST(World, DestroyInvalidEntity) {
    World world;
    ASSERT_FALSE(world.destroy_entity(kInvalidEntity));
}

TEST(World, DestroyAlreadyDestroyed) {
    World world;
    EntityId entity = world.create_entity();

    ASSERT_TRUE(world.destroy_entity(entity));
    ASSERT_FALSE(world.destroy_entity(entity));
}

// ========================================
// 带组件的实体创建测试
// ========================================

TEST(World, CreateEntityWithOneComponent) {
    World world;

    EntityId entity = world.create_entity(Position{1.0f, 2.0f, 3.0f});

    ASSERT_TRUE(world.is_alive(entity));
    ASSERT_EQ(world.entity_count(), 1u);
    ASSERT_EQ(world.archetype_count(), 1u);

    auto* pos = world.get_component<Position>(entity);
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(pos->x, 1.0f);
    ASSERT_EQ(pos->y, 2.0f);
    ASSERT_EQ(pos->z, 3.0f);
}

TEST(World, CreateEntityWithMultipleComponents) {
    World world;

    EntityId entity = world.create_entity(Position{10.0f, 20.0f, 0.0f}, Velocity{1.0f, 2.0f, 0.0f},
                                          Health{50, 100});

    ASSERT_TRUE(world.is_alive(entity));
    ASSERT_EQ(world.archetype_count(), 1u);

    auto* pos = world.get_component<Position>(entity);
    auto* vel = world.get_component<Velocity>(entity);
    auto* health = world.get_component<Health>(entity);

    ASSERT_TRUE(pos != nullptr);
    ASSERT_TRUE(vel != nullptr);
    ASSERT_TRUE(health != nullptr);

    ASSERT_EQ(pos->x, 10.0f);
    ASSERT_EQ(vel->vx, 1.0f);
    ASSERT_EQ(health->current, 50);
}

TEST(World, CreateEntitiesSameArchetype) {
    World world;

    EntityId e1 = world.create_entity(Position{1, 0, 0}, Velocity{1, 0, 0});
    EntityId e2 = world.create_entity(Position{2, 0, 0}, Velocity{2, 0, 0});
    EntityId e3 = world.create_entity(Position{3, 0, 0}, Velocity{3, 0, 0});

    // 相同组件组合应该使用同一个 Archetype
    ASSERT_EQ(world.archetype_count(), 1u);
    ASSERT_EQ(world.entity_count(), 3u);

    ASSERT_EQ(world.get_component<Position>(e1)->x, 1.0f);
    ASSERT_EQ(world.get_component<Position>(e2)->x, 2.0f);
    ASSERT_EQ(world.get_component<Position>(e3)->x, 3.0f);
}

TEST(World, CreateEntitiesDifferentArchetypes) {
    World world;

    EntityId e1 = world.create_entity(Position{1, 0, 0});
    EntityId e2 = world.create_entity(Position{2, 0, 0}, Velocity{0, 0, 0});
    EntityId e3 = world.create_entity(Position{3, 0, 0}, Velocity{0, 0, 0}, Health{100, 100});

    // 不同组件组合应该创建不同的 Archetype
    ASSERT_EQ(world.archetype_count(), 3u);
    ASSERT_EQ(world.entity_count(), 3u);
}

// ========================================
// 组件操作测试
// ========================================

TEST(World, HasComponent) {
    World world;

    EntityId entity = world.create_entity(Position{0, 0, 0});

    ASSERT_TRUE(world.has_component<Position>(entity));
    ASSERT_FALSE(world.has_component<Velocity>(entity));
    ASSERT_FALSE(world.has_component<Health>(entity));
}

TEST(World, GetComponentNonExistent) {
    World world;

    EntityId entity = world.create_entity(Position{0, 0, 0});

    auto* vel = world.get_component<Velocity>(entity);
    ASSERT_TRUE(vel == nullptr);
}

TEST(World, SetComponent) {
    World world;

    EntityId entity = world.create_entity(Position{0, 0, 0});

    ASSERT_TRUE(world.set_component(entity, Position{100, 200, 300}));

    auto* pos = world.get_component<Position>(entity);
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(pos->x, 100.0f);
    ASSERT_EQ(pos->y, 200.0f);
    ASSERT_EQ(pos->z, 300.0f);
}

TEST(World, SetComponentNonExistent) {
    World world;

    EntityId entity = world.create_entity(Position{0, 0, 0});

    // 尝试设置不存在的组件应该失败
    ASSERT_FALSE(world.set_component(entity, Velocity{1, 1, 1}));
}

TEST(World, AddComponent) {
    World world;

    EntityId entity = world.create_entity(Position{1, 2, 3});
    ASSERT_EQ(world.archetype_count(), 1u);

    // 添加新组件
    ASSERT_TRUE(world.add_component(entity, Velocity{4, 5, 6}));

    // 应该创建新的 Archetype
    ASSERT_EQ(world.archetype_count(), 2u);

    // 验证组件值
    auto* pos = world.get_component<Position>(entity);
    auto* vel = world.get_component<Velocity>(entity);

    ASSERT_TRUE(pos != nullptr);
    ASSERT_TRUE(vel != nullptr);

    // 原有组件值应该保持不变
    ASSERT_EQ(pos->x, 1.0f);
    ASSERT_EQ(pos->y, 2.0f);
    ASSERT_EQ(pos->z, 3.0f);

    // 新组件应该有正确的值
    ASSERT_EQ(vel->vx, 4.0f);
    ASSERT_EQ(vel->vy, 5.0f);
    ASSERT_EQ(vel->vz, 6.0f);
}

TEST(World, AddComponentAlreadyExists) {
    World world;

    EntityId entity = world.create_entity(Position{1, 2, 3});

    // 尝试添加已存在的组件应该失败
    ASSERT_FALSE(world.add_component(entity, Position{4, 5, 6}));

    // 原值应该不变
    auto* pos = world.get_component<Position>(entity);
    ASSERT_EQ(pos->x, 1.0f);
}

TEST(World, RemoveComponent) {
    World world;

    EntityId entity = world.create_entity(Position{1, 2, 3}, Velocity{4, 5, 6});
    ASSERT_EQ(world.archetype_count(), 1u);

    // 移除组件
    ASSERT_TRUE(world.remove_component<Velocity>(entity));

    // 应该创建新的 Archetype
    ASSERT_EQ(world.archetype_count(), 2u);

    // 验证
    ASSERT_TRUE(world.has_component<Position>(entity));
    ASSERT_FALSE(world.has_component<Velocity>(entity));

    auto* pos = world.get_component<Position>(entity);
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(pos->x, 1.0f);
}

TEST(World, RemoveComponentNonExistent) {
    World world;

    EntityId entity = world.create_entity(Position{1, 2, 3});

    // 尝试移除不存在的组件应该失败
    ASSERT_FALSE(world.remove_component<Velocity>(entity));
}

TEST(World, RemoveAllComponents) {
    World world;

    EntityId entity = world.create_entity(Position{1, 2, 3});

    // 移除唯一的组件
    ASSERT_TRUE(world.remove_component<Position>(entity));

    // 实体仍然存活，但没有组件
    ASSERT_TRUE(world.is_alive(entity));
    ASSERT_FALSE(world.has_component<Position>(entity));
}

// ========================================
// 实体销毁与组件测试
// ========================================

TEST(World, DestroyEntityWithComponents) {
    World world;

    EntityId entity = world.create_entity(Position{1, 2, 3}, Velocity{4, 5, 6});

    ASSERT_TRUE(world.destroy_entity(entity));
    ASSERT_FALSE(world.is_alive(entity));

    // 尝试访问已销毁实体的组件
    ASSERT_TRUE(world.get_component<Position>(entity) == nullptr);
    ASSERT_TRUE(world.get_component<Velocity>(entity) == nullptr);
}

TEST(World, DestroyEntitySwapAndPop) {
    World world;

    // 创建多个实体
    EntityId e1 = world.create_entity(Position{1, 0, 0});
    EntityId e2 = world.create_entity(Position{2, 0, 0});
    EntityId e3 = world.create_entity(Position{3, 0, 0});

    // 销毁中间的实体（触发 swap-and-pop）
    ASSERT_TRUE(world.destroy_entity(e2));

    // 其他实体应该仍然可访问
    ASSERT_TRUE(world.is_alive(e1));
    ASSERT_FALSE(world.is_alive(e2));
    ASSERT_TRUE(world.is_alive(e3));

    // 验证组件值
    ASSERT_EQ(world.get_component<Position>(e1)->x, 1.0f);
    ASSERT_EQ(world.get_component<Position>(e3)->x, 3.0f);
}

// ========================================
// 遍历测试
// ========================================

TEST(World, EachSingleComponent) {
    World world;

    world.create_entity(Position{1, 0, 0});
    world.create_entity(Position{2, 0, 0});
    world.create_entity(Position{3, 0, 0});

    float sum = 0.0f;
    world.each<Position>([&sum](Position& pos) { sum += pos.x; });

    ASSERT_EQ(sum, 6.0f);
}

TEST(World, EachMultipleComponents) {
    World world;

    world.create_entity(Position{1, 0, 0}, Velocity{10, 0, 0});
    world.create_entity(Position{2, 0, 0}, Velocity{20, 0, 0});

    // 还有一个只有 Position 的实体
    world.create_entity(Position{100, 0, 0});

    float sum = 0.0f;
    world.each<Position, Velocity>([&sum](Position& pos, Velocity& vel) { sum += pos.x + vel.vx; });

    // 只有前两个实体匹配
    ASSERT_EQ(sum, 1.0f + 10.0f + 2.0f + 20.0f);
}

TEST(World, EachModifyComponents) {
    World world;

    world.create_entity(Position{0, 0, 0}, Velocity{1, 0, 0});
    world.create_entity(Position{0, 0, 0}, Velocity{2, 0, 0});
    world.create_entity(Position{0, 0, 0}, Velocity{3, 0, 0});

    // 模拟运动系统
    world.each<Position, Velocity>([](Position& pos, Velocity& vel) { pos.x += vel.vx; });

    float sum = 0.0f;
    world.each<Position>([&sum](Position& pos) { sum += pos.x; });

    ASSERT_EQ(sum, 6.0f);
}

// ========================================
// 非平凡类型测试
// ========================================

TEST(World, NonTrivialComponent) {
    World world;

    EntityId entity = world.create_entity(Name{"TestEntity"}, Position{1, 2, 3});

    auto* name = world.get_component<Name>(entity);
    ASSERT_TRUE(name != nullptr);
    ASSERT_EQ(name->value, "TestEntity");

    // 修改
    ASSERT_TRUE(world.set_component(entity, Name{"ModifiedName"}));
    ASSERT_EQ(world.get_component<Name>(entity)->value, "ModifiedName");
}

TEST(World, AddNonTrivialComponent) {
    World world;

    EntityId entity = world.create_entity(Position{1, 2, 3});

    ASSERT_TRUE(world.add_component(entity, Name{"AddedName"}));

    auto* name = world.get_component<Name>(entity);
    ASSERT_TRUE(name != nullptr);
    ASSERT_EQ(name->value, "AddedName");
}

// ========================================
// 标签组件测试
// ========================================

TEST(World, TagComponent) {
    World world;

    EntityId enemy = world.create_entity(Position{0, 0, 0}, EnemyTag{});
    EntityId player = world.create_entity(Position{0, 0, 0}, PlayerTag{});

    ASSERT_TRUE(world.has_component<EnemyTag>(enemy));
    ASSERT_FALSE(world.has_component<PlayerTag>(enemy));

    ASSERT_FALSE(world.has_component<EnemyTag>(player));
    ASSERT_TRUE(world.has_component<PlayerTag>(player));
}

// ========================================
// 压力测试
// ========================================

TEST(World, StressCreateDestroy) {
    World world;

    std::vector<EntityId> entities;
    entities.reserve(1000);

    // 创建 1000 个实体
    for (int i = 0; i < 1000; ++i) {
        entities.push_back(
            world.create_entity(Position{static_cast<float>(i), 0, 0}, Velocity{1, 0, 0}));
    }

    ASSERT_EQ(world.entity_count(), 1000u);

    // 销毁一半
    for (int i = 0; i < 500; ++i) {
        world.destroy_entity(entities[static_cast<std::size_t>(i) * 2]);
    }

    ASSERT_EQ(world.entity_count(), 500u);

    // 验证剩余实体
    for (int i = 0; i < 500; ++i) {
        EntityId e = entities[static_cast<std::size_t>(i) * 2 + 1];
        ASSERT_TRUE(world.is_alive(e));
    }
}

TEST(World, StressMigration) {
    World world;

    // 创建实体
    EntityId entity = world.create_entity(Position{0, 0, 0});

    // 反复添加/移除组件
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(world.add_component(entity, Velocity{static_cast<float>(i), 0, 0}));
        ASSERT_TRUE(world.has_component<Velocity>(entity));

        ASSERT_TRUE(world.remove_component<Velocity>(entity));
        ASSERT_FALSE(world.has_component<Velocity>(entity));
    }

    // 实体应该仍然存活且有 Position
    ASSERT_TRUE(world.is_alive(entity));
    ASSERT_TRUE(world.has_component<Position>(entity));
}

// ========================================
// Move 语义测试
// ========================================

TEST(World, MoveConstruction) {
    World world1;
    world1.create_entity(Position{1, 2, 3});
    world1.create_entity(Position{4, 5, 6});

    World world2(std::move(world1));

    ASSERT_EQ(world2.entity_count(), 2u);
    ASSERT_EQ(world2.archetype_count(), 1u);
}

TEST(World, MoveAssignment) {
    World world1;
    world1.create_entity(Position{1, 2, 3});

    World world2;
    world2.create_entity(Position{7, 8, 9}, Velocity{1, 0, 0});

    world2 = std::move(world1);

    ASSERT_EQ(world2.entity_count(), 1u);
    ASSERT_EQ(world2.archetype_count(), 1u);
}

// ========================================
// 主函数
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
