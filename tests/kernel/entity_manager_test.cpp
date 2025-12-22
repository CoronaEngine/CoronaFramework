#include "corona/kernel/ecs/entity_manager.h"

#include <algorithm>
#include <set>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/ecs/entity_id.h"
#include "corona/kernel/ecs/entity_record.h"

using namespace Corona::Kernel::ECS;
using namespace CoronaTest;

// ========================================
// EntityId 测试
// ========================================

TEST(EntityId, DefaultConstruction) {
    EntityId id;
    ASSERT_FALSE(id.is_valid());
    ASSERT_EQ(id.index(), EntityId::kInvalidIndex);
    ASSERT_EQ(id.generation(), EntityId::kInvalidGeneration);
}

TEST(EntityId, Construction) {
    EntityId id(42, 1);
    ASSERT_TRUE(id.is_valid());
    ASSERT_EQ(id.index(), 42u);
    ASSERT_EQ(id.generation(), 1u);
}

TEST(EntityId, FromRaw) {
    EntityId id1(100, 5);
    EntityId id2 = EntityId::from_raw(id1.raw());
    ASSERT_EQ(id1, id2);
    ASSERT_EQ(id2.index(), 100u);
    ASSERT_EQ(id2.generation(), 5u);
}

TEST(EntityId, InvalidConstant) {
    EntityId invalid = EntityId::invalid();
    ASSERT_FALSE(invalid.is_valid());
    ASSERT_EQ(invalid, kInvalidEntity);
}

TEST(EntityId, Comparison) {
    EntityId id1(10, 1);
    EntityId id2(10, 1);
    EntityId id3(10, 2);
    EntityId id4(20, 1);

    ASSERT_EQ(id1, id2);
    ASSERT_NE(id1, id3);  // 不同版本号
    ASSERT_NE(id1, id4);  // 不同索引
}

TEST(EntityId, Ordering) {
    EntityId id1(10, 1);
    EntityId id2(20, 1);
    EntityId id3(10, 2);

    // 按索引排序（索引在高位）
    ASSERT_TRUE(id1 < id2);
    ASSERT_TRUE(id1 < id3);  // 相同索引时按版本号
}

TEST(EntityId, Hash) {
    EntityId id1(10, 1);
    EntityId id2(10, 1);
    EntityId id3(10, 2);

    std::hash<EntityId> hasher;
    ASSERT_EQ(hasher(id1), hasher(id2));
    ASSERT_NE(hasher(id1), hasher(id3));
}

// ========================================
// EntityRecord 测试
// ========================================

TEST(EntityRecord, DefaultConstruction) {
    EntityRecord record;
    ASSERT_FALSE(record.is_alive());
    ASSERT_EQ(record.archetype_id, kInvalidArchetypeId);
    ASSERT_EQ(record.generation, 0u);
}

TEST(EntityRecord, IsAlive) {
    EntityRecord record;
    ASSERT_FALSE(record.is_alive());

    record.archetype_id = 1;
    ASSERT_TRUE(record.is_alive());

    record.archetype_id = kInvalidArchetypeId;
    ASSERT_FALSE(record.is_alive());
}

TEST(EntityRecord, Reset) {
    EntityRecord record;
    record.archetype_id = 5;
    record.location = {1, 2};
    record.generation = 10;

    record.reset();

    ASSERT_FALSE(record.is_alive());
    ASSERT_EQ(record.archetype_id, kInvalidArchetypeId);
    ASSERT_EQ(record.location.chunk_index, 0u);
    ASSERT_EQ(record.location.index_in_chunk, 0u);
    // generation 不应该被重置
    ASSERT_EQ(record.generation, 10u);
}

TEST(EntityRecord, Clear) {
    EntityRecord record;
    record.archetype_id = 5;
    record.location = {1, 2};
    record.generation = 10;

    record.clear();

    ASSERT_FALSE(record.is_alive());
    ASSERT_EQ(record.archetype_id, kInvalidArchetypeId);
    ASSERT_EQ(record.generation, 0u);  // clear 会重置 generation
}

// ========================================
// EntityManager 测试
// ========================================

TEST(EntityManager, DefaultConstruction) {
    EntityManager manager;
    ASSERT_EQ(manager.alive_count(), 0u);
    ASSERT_EQ(manager.free_count(), 0u);
}

TEST(EntityManager, ConstructionWithCapacity) {
    EntityManager manager(100);
    ASSERT_EQ(manager.alive_count(), 0u);
}

TEST(EntityManager, CreateSingleEntity) {
    EntityManager manager;
    EntityId id = manager.create();

    ASSERT_TRUE(id.is_valid());
    ASSERT_TRUE(manager.is_alive(id));
    ASSERT_EQ(manager.alive_count(), 1u);
    ASSERT_EQ(id.index(), 0u);
    ASSERT_EQ(id.generation(), 1u);  // 初始版本号为 1
}

TEST(EntityManager, CreateMultipleEntities) {
    EntityManager manager;
    std::vector<EntityId> ids;

    for (int i = 0; i < 100; ++i) {
        ids.push_back(manager.create());
    }

    ASSERT_EQ(manager.alive_count(), 100u);

    // 验证所有 ID 唯一
    std::set<std::uint64_t> raw_ids;
    for (const auto& id : ids) {
        ASSERT_TRUE(id.is_valid());
        ASSERT_TRUE(manager.is_alive(id));
        raw_ids.insert(id.raw());
    }
    ASSERT_EQ(raw_ids.size(), 100u);
}

TEST(EntityManager, DestroyEntity) {
    EntityManager manager;
    EntityId id = manager.create();

    ASSERT_TRUE(manager.is_alive(id));
    ASSERT_TRUE(manager.destroy(id));
    ASSERT_FALSE(manager.is_alive(id));
    ASSERT_EQ(manager.alive_count(), 0u);
    ASSERT_EQ(manager.free_count(), 1u);
}

TEST(EntityManager, DestroyInvalidEntity) {
    EntityManager manager;
    EntityId invalid;

    ASSERT_FALSE(manager.destroy(invalid));
}

TEST(EntityManager, DestroyAlreadyDestroyed) {
    EntityManager manager;
    EntityId id = manager.create();

    ASSERT_TRUE(manager.destroy(id));
    ASSERT_FALSE(manager.destroy(id));  // 重复销毁应返回 false
}

TEST(EntityManager, IdReuse) {
    EntityManager manager;

    // 创建并销毁实体
    EntityId id1 = manager.create();
    auto index1 = id1.index();
    auto gen1 = id1.generation();

    ASSERT_TRUE(manager.destroy(id1));

    // 创建新实体应复用索引
    EntityId id2 = manager.create();
    ASSERT_EQ(id2.index(), index1);
    ASSERT_EQ(id2.generation(), gen1 + 1);  // 版本号递增

    // 旧 ID 应该无效
    ASSERT_FALSE(manager.is_alive(id1));
    ASSERT_TRUE(manager.is_alive(id2));
}

TEST(EntityManager, GenerationOverflow) {
    EntityManager manager;

    // 反复创建销毁同一个槽位
    EntityId last_id;
    for (int i = 0; i < 100; ++i) {
        EntityId id = manager.create();
        manager.destroy(id);
        last_id = manager.create();
        manager.destroy(last_id);
    }

    // 版本号应该递增
    ASSERT_GT(last_id.generation(), 100u);
}

TEST(EntityManager, GetRecord) {
    EntityManager manager;
    EntityId id = manager.create();

    EntityRecord* record = manager.get_record(id);
    ASSERT_TRUE(record != nullptr);
    ASSERT_EQ(record->generation, id.generation());
}

TEST(EntityManager, GetRecordInvalidId) {
    EntityManager manager;
    EntityId invalid;

    ASSERT_TRUE(manager.get_record(invalid) == nullptr);
}

TEST(EntityManager, GetRecordDestroyedEntity) {
    EntityManager manager;
    EntityId id = manager.create();
    manager.destroy(id);

    // 使用旧 ID 应该返回 nullptr
    ASSERT_TRUE(manager.get_record(id) == nullptr);
}

TEST(EntityManager, UpdateLocation) {
    EntityManager manager;
    EntityId id = manager.create();

    EntityLocation loc{2, 5};
    ASSERT_TRUE(manager.update_location(id, 10, loc));

    const EntityRecord* record = manager.get_record(id);
    ASSERT_TRUE(record != nullptr);
    ASSERT_EQ(record->archetype_id, 10u);
    ASSERT_EQ(record->location.chunk_index, 2u);
    ASSERT_EQ(record->location.index_in_chunk, 5u);
}

TEST(EntityManager, UpdateLocationInvalidId) {
    EntityManager manager;
    EntityId invalid;

    EntityLocation loc{0, 0};
    ASSERT_FALSE(manager.update_location(invalid, 1, loc));
}

TEST(EntityManager, Reserve) {
    EntityManager manager;
    manager.reserve(1000);

    // 创建大量实体应该不需要重新分配
    for (int i = 0; i < 1000; ++i) {
        (void)manager.create();
    }

    ASSERT_EQ(manager.alive_count(), 1000u);
}

TEST(EntityManager, Clear) {
    EntityManager manager;

    std::vector<EntityId> ids;
    for (int i = 0; i < 100; ++i) {
        ids.push_back(manager.create());
    }

    manager.clear();

    ASSERT_EQ(manager.alive_count(), 0u);
    ASSERT_EQ(manager.free_count(), 0u);

    // 所有旧 ID 应该无效
    for (const auto& id : ids) {
        ASSERT_FALSE(manager.is_alive(id));
    }
}

TEST(EntityManager, CreateAfterClear) {
    EntityManager manager;

    for (int i = 0; i < 10; ++i) {
        (void)manager.create();
    }

    manager.clear();

    // 清空后创建新实体
    EntityId id = manager.create();
    ASSERT_TRUE(id.is_valid());
    ASSERT_TRUE(manager.is_alive(id));
    ASSERT_EQ(manager.alive_count(), 1u);
}

TEST(EntityManager, DanglingReferenceDetection) {
    EntityManager manager;

    EntityId enemy = manager.create();
    manager.update_location(enemy, 1, {0, 0});

    // 销毁实体
    manager.destroy(enemy);

    // 创建新实体（可能复用同一索引）
    EntityId npc = manager.create();

    // 旧引用应该失效
    ASSERT_FALSE(manager.is_alive(enemy));
    ASSERT_TRUE(manager.is_alive(npc));

    // 使用旧 ID 获取记录应该失败
    ASSERT_TRUE(manager.get_record(enemy) == nullptr);
}

TEST(EntityManager, StressTest) {
    EntityManager manager;
    std::vector<EntityId> active;

    // 随机创建和销毁
    for (int i = 0; i < 1000; ++i) {
        // 创建 3 个
        for (int j = 0; j < 3; ++j) {
            active.push_back(manager.create());
        }

        // 销毁 1 个（如果有的话）
        if (!active.empty()) {
            auto idx = static_cast<std::size_t>(i) % active.size();
            EntityId to_destroy = active[idx];
            if (manager.is_alive(to_destroy)) {
                manager.destroy(to_destroy);
            }
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    }

    // 验证所有活跃实体确实存活
    std::size_t actual_alive = 0;
    for (const auto& id : active) {
        if (manager.is_alive(id)) {
            ++actual_alive;
        }
    }

    ASSERT_EQ(actual_alive, manager.alive_count());
}

// ========================================
// 主函数
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
