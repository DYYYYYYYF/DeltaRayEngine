#pragma once

#include "Defines.hpp"
#include "Core/DMemory.hpp"

/**
 * @brief 开放寻址（Robin Hood）哈希表，替代 std::unordered_map。
 *
 * 特性：
 *  - 完全模板化，类型安全
 *  - Robin Hood 线性探测，减少平均探测长度
 *  - 负载因子超过 0.75 时自动扩容（容量翻倍）
 *  - 不依赖任何 std 容器
 *  - 支持自定义哈希仿函数（默认 TDefaultHasher<K>）
 *
 * 使用示例：
 *   TMap<FString, uint32_t> FontMap;
 *   FontMap.Insert("Arial", 42);
 *   uint32_t* v = FontMap.Find("Arial");
 *   FontMap.Remove("Arial");
 *
 *   // 范围 for（Key/Value 风格）
 *   for (auto& Pair : FontMap) {
 *       Pair.Key;   Pair.Value;
 *       Pair.First(); Pair.Second();  // 等价写法
 *   }
 */

 // ============================================================
 //  默认哈希器（可针对具体类型特化）
 // ============================================================

// ── 通用回退：枚举等可按整数转换的键类型（无显式特化时使用） ──
template<typename K>
struct TDefaultHasher {
    size_t operator()(const K& key) const noexcept {
        return TDefaultHasher<uint64_t>{}(static_cast<uint64_t>(key));
    }
};

// ── const char* 特化 ────────────────────────────────────────
template<>
struct TDefaultHasher<const char*> {
    size_t operator()(const char* key) const noexcept {
        size_t hash = 5381;
        while (*key)
            hash = ((hash << 5) + hash) ^ static_cast<unsigned char>(*key++);
        return hash;
    }
};

// ── 整数类型通用特化 ─────────────────────────────────────────
template<> struct TDefaultHasher<uint8_t> { size_t operator()(uint8_t  v) const noexcept { return static_cast<size_t>(v); } };
template<> struct TDefaultHasher<uint16_t> { size_t operator()(uint16_t v) const noexcept { return static_cast<size_t>(v); } };
template<> struct TDefaultHasher<uint32_t> { size_t operator()(uint32_t v) const noexcept { return static_cast<size_t>(v) * 2654435761ULL; } };
template<> struct TDefaultHasher<uint64_t> { size_t operator()(uint64_t v) const noexcept { v ^= v >> 33; v *= 0xff51afd7ed558ccdULL; v ^= v >> 33; return static_cast<size_t>(v); } };
template<> struct TDefaultHasher<int32_t> { size_t operator()(int32_t  v) const noexcept { return TDefaultHasher<uint32_t>{}(static_cast<uint32_t>(v)); } };
template<> struct TDefaultHasher<int64_t> { size_t operator()(int64_t  v) const noexcept { return TDefaultHasher<uint64_t>{}(static_cast<uint64_t>(v)); } };

// ── 指针特化 ─────────────────────────────────────────────────
template<typename T>
struct TDefaultHasher<T*> {
    size_t operator()(T* ptr) const noexcept {
        return TDefaultHasher<uint64_t>{}(reinterpret_cast<uint64_t>(ptr));
    }
};

// ============================================================
//  TMap
// ============================================================

template<typename K, typename V, typename Hasher = TDefaultHasher<K>>
class DAPI TMap {
public:
    // ── 公开类型别名 ──────────────────────────────────────
    /**
     * @brief 键值对结构体。
     * 同时支持两种访问风格：
     *   pair.Key   / pair.Value    （引擎风格）
     *   pair.First()/ pair.Second() （兼容 std::pair 风格）
     */
    struct Pair {
        K Key;
        V Value;

        K& First() { return Key; }
        const K& First()  const { return Key; }
        V& Second() { return Value; }
        const V& Second() const { return Value; }
    };

private:
    // ── 内部桶状态 ────────────────────────────────────────
    enum class EBucketState : uint8_t {
        Empty = 0,
        Occupied,
        Deleted     // 墓碑，用于支持删除后继续探测
    };

    struct Bucket {
        Pair         Data;
        EBucketState State = EBucketState::Empty;
        uint32_t     ProbeLen = 0;   // Robin Hood 探测距离
    };

    static constexpr size_t   kInitialCapacity = 16;
    static constexpr float    kMaxLoadFactor = 0.75f;

public:
    // ─────────────────────────────────────────────────────
    //  构造 / 析构
    // ─────────────────────────────────────────────────────
    TMap() : Buckets_(nullptr), Capacity_(0), Count_(0) {
        Resize(kInitialCapacity);
    }

    explicit TMap(size_t initial_capacity)
        : Buckets_(nullptr), Capacity_(0), Count_(0) {
        // 向上取到 2 的幂
        size_t cap = kInitialCapacity;
        while (cap < initial_capacity) cap <<= 1;
        Resize(cap);
    }

    TMap(const TMap& other) : Buckets_(nullptr), Capacity_(0), Count_(0) {
        Resize(other.Capacity_);
        for (size_t i = 0; i < other.Capacity_; ++i) {
            if (other.Buckets_[i].State == EBucketState::Occupied)
                Insert(other.Buckets_[i].Data.Key, other.Buckets_[i].Data.Value);
        }
    }

    TMap(TMap&& other) noexcept
        : Buckets_(other.Buckets_), Capacity_(other.Capacity_), Count_(other.Count_) {
        other.Buckets_ = nullptr;
        other.Capacity_ = 0;
        other.Count_ = 0;
    }

    ~TMap() { FreeBuckets(); }

    TMap& operator=(const TMap& other) {
        if (this == &other) return *this;
        FreeBuckets();
        Resize(other.Capacity_);
        for (size_t i = 0; i < other.Capacity_; ++i)
            if (other.Buckets_[i].State == EBucketState::Occupied)
                Insert(other.Buckets_[i].Data.Key, other.Buckets_[i].Data.Value);
        return *this;
    }

    TMap& operator=(TMap&& other) noexcept {
        if (this != &other) {
            FreeBuckets();
            Buckets_ = other.Buckets_;
            Capacity_ = other.Capacity_;
            Count_ = other.Count_;
            other.Buckets_ = nullptr;
            other.Capacity_ = 0;
            other.Count_ = 0;
        }
        return *this;
    }

    // ─────────────────────────────────────────────────────
    //  基本操作
    // ─────────────────────────────────────────────────────

    /**
     * @brief 插入或覆盖键值对。
     * @return 指向最终存储值的指针。
     */
    V* Insert(const K& key, const V& value) {
        MaybeGrow();
        return InsertInternal(key, value);
    }

    V* Insert(const K& key, V&& value) {
        MaybeGrow();
        return InsertInternal(key, static_cast<V&&>(value));
    }

    /**
     * @brief 查找键，返回值指针；不存在时返回 nullptr。
     */
    V* Find(const K& key) {
        size_t idx = FindBucket(key);
        if (idx == kInvalid) return nullptr;
        return &Buckets_[idx].Data.Value;
    }

    const V* Find(const K& key) const {
        size_t idx = FindBucket(key);
        if (idx == kInvalid) return nullptr;
        return &Buckets_[idx].Data.Value;
    }

    /*
    * @brief 查找键，确保存在时调用
    */
    V& At(const K& key) {
		size_t idx = FindBucket(key);
        if (idx == kInvalid) GLOG(Log::eError, "Please call TMap::At() function after confirm.");
		return Buckets_[idx].Data.Value;
    }

    const V& At(const K& key) const {
		size_t idx = FindBucket(key);
		if (idx == kInvalid) GLOG(Log::eError, "Please call TMap::At() function after confirm.");
		return Buckets_[idx].Data.Value;
    }

    /**
     * @brief 判断键是否存在。
     */
    bool Contains(const K& key) const { return FindBucket(key) != kInvalid; }

    /**
     * @brief 获取键对应的完整 Pair（Key + Value）。
     * 调用前请先用 Contains() 确认键存在，否则触发断言。
     *
     * 典型用法：
     *   if (Map.Contains(ID)) {
     *       const TMap<K,V>::Pair& p = Map.Get(ID);
     *       p.Key;  p.Value;
     *       p.First(); p.Second();
     *   }
     */
    Pair& Get(const K& key) {
        size_t idx = FindBucket(key);
        if (idx == kInvalid) {
            GLOG(Log::eError, "TMap::Get: key does not exist, call Contains() first");
            ASSERT(false);
        }

        return Buckets_[idx].Data;
    }

    const Pair& Get(const K& key) const {
        size_t idx = FindBucket(key);
		if (idx == kInvalid) {
            GLOG(Log::eError, "TMap::Get: key does not exist, call Contains() first");
			ASSERT(false);
		}

        return Buckets_[idx].Data;
    }

    /**
     * @brief 尝试获取 Pair，通过 out_pair 输出，键不存在时返回 false。
     * 不需要提前调用 Contains()，一次调用完成判断 + 获取。
     *
     * 典型用法：
     *   TMap<K,V>::Pair Pair;
     *   if (Map.TryGet(ID, Pair)) {
     *       Pair.Key;  Pair.Value;
     *   }
     */
    bool TryGet(const K& key, Pair& out_pair) {
        size_t idx = FindBucket(key);
        if (idx == kInvalid) return false;
        out_pair = Buckets_[idx].Data;
        return true;
    }

    bool TryGet(const K& key, const Pair*& out_pair) const {
        size_t idx = FindBucket(key);
        if (idx == kInvalid) return false;
        out_pair = &Buckets_[idx].Data;
        return true;
    }

    /**
     * @brief 下标访问，不存在时默认构造插入。
     */
    V& operator[](const K& key) {
        size_t idx = FindBucket(key);
        if (idx != kInvalid) return Buckets_[idx].Data.Value;
        V* inserted = Insert(key, V{});
        return *inserted;
    }

    /**
     * @brief 移除键，成功返回 true。
     */
    bool Remove(const K& key) {
        size_t idx = FindBucket(key);
        if (idx == kInvalid) return false;
        Buckets_[idx].State = EBucketState::Deleted;
        Buckets_[idx].Data.Key = K{};
        Buckets_[idx].Data.Value = V{};
        --Count_;
        return true;
    }

    /**
     * @brief 清空所有条目（保留已分配容量）。
     */
    void Clear() {
        for (size_t i = 0; i < Capacity_; ++i)
            Buckets_[i].State = EBucketState::Empty;
        Count_ = 0;
    }

    // ─────────────────────────────────────────────────────
    //  容量查询
    // ─────────────────────────────────────────────────────
    size_t Size()     const { return Count_; }
    size_t Capacity() const { return Capacity_; }
    bool   IsEmpty()  const { return Count_ == 0; }

    // ─────────────────────────────────────────────────────
    //  迭代器（只前向，跳过空桶和墓碑）
    // ─────────────────────────────────────────────────────
    struct Iterator {
        Bucket* Ptr;
        Bucket* End;

        Iterator(Bucket* ptr, Bucket* end) : Ptr(ptr), End(end) {
            SkipEmpty();
        }

        Pair& operator*()  const { return Ptr->Data; }
        Pair* operator->() const { return &Ptr->Data; }

        Iterator& operator++() {
            ++Ptr;
            SkipEmpty();
            return *this;
        }

        bool operator==(const Iterator& o) const { return Ptr == o.Ptr; }
        bool operator!=(const Iterator& o) const { return Ptr != o.Ptr; }

    private:
        void SkipEmpty() {
            while (Ptr != End && Ptr->State != EBucketState::Occupied)
                ++Ptr;
        }
    };

    struct ConstIterator {
        const Bucket* Ptr;
        const Bucket* End;

        ConstIterator(const Bucket* ptr, const Bucket* end) : Ptr(ptr), End(end) {
            SkipEmpty();
        }

        const Pair& operator*()  const { return Ptr->Data; }
        const Pair* operator->() const { return &Ptr->Data; }

        ConstIterator& operator++() { ++Ptr; SkipEmpty(); return *this; }
        bool operator==(const ConstIterator& o) const { return Ptr == o.Ptr; }
        bool operator!=(const ConstIterator& o) const { return Ptr != o.Ptr; }

    private:
        void SkipEmpty() {
            while (Ptr != End && Ptr->State != EBucketState::Occupied)
                ++Ptr;
        }
    };

    Iterator      begin() { return Iterator(Buckets_, Buckets_ + Capacity_); }
    Iterator      end() { return Iterator(Buckets_ + Capacity_, Buckets_ + Capacity_); }
    ConstIterator begin()  const { return ConstIterator(Buckets_, Buckets_ + Capacity_); }
    ConstIterator end()    const { return ConstIterator(Buckets_ + Capacity_, Buckets_ + Capacity_); }

private:
    // ─────────────────────────────────────────────────────
    //  内部实现
    // ─────────────────────────────────────────────────────
    static constexpr size_t kInvalid = static_cast<size_t>(-1);

    Bucket* Buckets_;
    size_t  Capacity_;
    size_t  Count_;
    Hasher  Hasher_;

    // 分配并零初始化桶数组
    void Resize(size_t new_capacity) {
        Bucket* new_buckets = (Bucket*)Memory::Allocate(
            new_capacity * sizeof(Bucket), MemoryType::eMemory_Type_Map);
        if (!new_buckets) {
            GLOG(Log::eFatal, "TMap::Resize: allocation failed");
            return;
        }
        // placement new 初始化每个桶
        for (size_t i = 0; i < new_capacity; ++i)
            new (new_buckets + i) Bucket();

        Bucket* old_buckets = Buckets_;
        size_t  old_capacity = Capacity_;

        Buckets_ = new_buckets;
        Capacity_ = new_capacity;
        Count_ = 0;

        // 将旧桶数据 rehash 到新数组
        if (old_buckets) {
            for (size_t i = 0; i < old_capacity; ++i) {
                if (old_buckets[i].State == EBucketState::Occupied)
                    InsertInternal(old_buckets[i].Data.Key,
                        static_cast<V&&>(old_buckets[i].Data.Value));
            }
            // 析构旧桶
            for (size_t i = 0; i < old_capacity; ++i)
                old_buckets[i].~Bucket();
            Memory::Free(old_buckets, MemoryType::eMemory_Type_Map);
        }
    }

    void FreeBuckets() {
        if (Buckets_) {
            for (size_t i = 0; i < Capacity_; ++i)
                Buckets_[i].~Bucket();
            Memory::Free(Buckets_, MemoryType::eMemory_Type_Map);
            Buckets_ = nullptr;
            Capacity_ = 0;
            Count_ = 0;
        }
    }

    void MaybeGrow() {
        if (Count_ + 1 > static_cast<size_t>(Capacity_ * kMaxLoadFactor))
            Resize(Capacity_ << 1);  // 容量翻倍
    }

    // Robin Hood 插入
    template<typename VV>
    V* InsertInternal(const K& key, VV&& value) {
        size_t hash = Hasher_(key) & (Capacity_ - 1);
        uint32_t probe = 0;

        // 当前"正在插入"的候选条目
        Bucket candidate;
        candidate.Data.Key = key;
        candidate.Data.Value = static_cast<VV&&>(value);
        candidate.State = EBucketState::Occupied;
        candidate.ProbeLen = 0;

        V* result = nullptr;

        for (;;) {
            size_t idx = (hash + probe) & (Capacity_ - 1);
            Bucket& slot = Buckets_[idx];

            if (slot.State == EBucketState::Empty ||
                slot.State == EBucketState::Deleted) {
                // 找到空槽：放入候选
                if (!result) result = &slot.Data.Value;
                slot = std::move(candidate);
                ++Count_;
                return result;
            }

            // 键相同：覆盖
            if (slot.Data.Key == candidate.Data.Key) {
                slot.Data.Value = static_cast<VV&&>(candidate.Data.Value);
                return &slot.Data.Value;
            }

            // Robin Hood：如果现有条目的探测距离更短，则抢占
            if (slot.ProbeLen < candidate.ProbeLen) {
                if (!result) result = &slot.Data.Value;
                // 交换 candidate 和 slot
                Bucket tmp = std::move(slot);
                slot = std::move(candidate);
                candidate = std::move(tmp);
            }

            probe++;
            candidate.ProbeLen++;
        }
    }

    // 查找桶下标，未找到返回 kInvalid
    size_t FindBucket(const K& key) const {
        if (Capacity_ == 0) return kInvalid;
        size_t hash = Hasher_(key) & (Capacity_ - 1);
        uint32_t probe = 0;

        for (;;) {
            size_t idx = (hash + probe) & (Capacity_ - 1);
            const Bucket& slot = Buckets_[idx];

            if (slot.State == EBucketState::Empty)
                return kInvalid;

            if (slot.State == EBucketState::Occupied &&
                slot.Data.Key == key)
                return idx;

            // Robin Hood 早停：现有条目探测距离更短意味着目标不可能在更后面
            if (slot.ProbeLen < probe)
                return kInvalid;

            ++probe;
        }
    }


};

// ============================================================
//  TSet —— 基于 TMap 实现，value 为 bool 占位
// ============================================================

template<typename K, typename Hasher = TDefaultHasher<K>>
class DAPI TSet {
public:
    TSet() = default;
    explicit TSet(size_t initial_capacity) : Map_(initial_capacity) {}

    bool     Insert(const K& key) { Map_.Insert(key, true); return true; }
    bool     Contains(const K& key) const { return Map_.Contains(key); }
    bool     Remove(const K& key) { return Map_.Remove(key); }
    void     Clear() { Map_.Clear(); }
    size_t   Size() const { return Map_.Size(); }
    bool     IsEmpty() const { return Map_.IsEmpty(); }

    // 迭代器：只暴露 Key
    struct Iterator {
        typename TMap<K, bool, Hasher>::Iterator Inner;
        Iterator(typename TMap<K, bool, Hasher>::Iterator it) : Inner(it) {}
        const K& operator*()  const { return Inner->Key; }
        const K* operator->() const { return &Inner->Key; }
        Iterator& operator++() { ++Inner; return *this; }
        bool operator==(const Iterator& o) const { return Inner == o.Inner; }
        bool operator!=(const Iterator& o) const { return Inner != o.Inner; }
    };

    Iterator begin() { return Iterator(Map_.begin()); }
    Iterator end() { return Iterator(Map_.end()); }

private:
    TMap<K, bool, Hasher> Map_;
};