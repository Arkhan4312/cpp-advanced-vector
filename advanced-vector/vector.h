#pragma once
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity)), capacity_(capacity) {}

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним
        // элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи
    // Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() noexcept = default;

    explicit Vector(std::size_t size) : data_(size) {
        if (size != 0) {
            std::uninitialized_value_construct_n(data_.GetAddress(), size);
        }
        size_ = size;
    }

    Vector(const Vector& other) : data_(other.size_) {
        if (other.size_ != 0) {
            std::uninitialized_copy_n(other.data_.GetAddress(), other.size_,
                data_.GetAddress());
        }
        size_ = other.size_;
    }

    Vector(Vector&& other) noexcept : data_() {
        data_.Swap(other.data_);
        size_ = std::exchange(other.size_, 0);
    }

    Vector& operator=(const Vector& rhs) {
        if (this == &rhs) {
            return *this;
        }
        if (Capacity() >= rhs.size_) {
            AssignFrom(rhs);
        } else {
            Vector tmp(rhs);
            Swap(tmp);
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            data_.Swap(rhs.data_);
            size_ = std::exchange(rhs.size_, 0);
        }
        return *this;
    }

    ~Vector() {
        DestroyElements();
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        assert(index < size_);
        return data_[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    void Reserve(std::size_t new_capacity) {
        if (new_capacity <= Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        Relocate(data_.GetAddress(), size_, new_data.GetAddress());

        DestroyElements();
        data_.Swap(new_data);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            size_ = new_size;
            return;
        }
        if (new_size == size_) {
            return;
        }

        Reserve(new_size);

        std::uninitialized_value_construct_n(data_.GetAddress() + size_,
            new_size - size_);
        size_ = new_size;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ < Capacity()) {
            T* ptr = data_.GetAddress() + size_;
            new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
            ++size_;
            return *ptr;
        }

        const size_t new_capacity = NextCapacity();
        RawMemory<T> new_data(new_capacity);

        T* new_elem = new_data.GetAddress() + size_;
        new (static_cast<void*>(new_elem)) T(std::forward<Args>(args)...);

        try {
            Relocate(data_.GetAddress(), size_, new_data.GetAddress());
        } catch (...) {
            std::destroy_at(new_elem);
            throw;
        }

        DestroyElements();
        data_.Swap(new_data);
        ++size_;
        return data_[size_ - 1];
    }

    void PopBack() noexcept {
        assert(size_ > 0);
        std::destroy_at(data_.GetAddress() + size_ - 1);
        --size_;
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        const size_t index = static_cast<size_t>(pos - data_.GetAddress());
        assert(index <= size_);

        if (index == size_) {
            return &EmplaceBack(std::forward<Args>(args)...);
        }

        if (size_ < Capacity()) {
            return EmplaceInPlace(index, std::forward<Args>(args)...);
        }

        return EmplaceWithRealloc(index, std::forward<Args>(args)...);
    }

    iterator Erase(const_iterator pos) noexcept(
        std::is_nothrow_move_assignable_v<T>) {
        const size_t index = static_cast<size_t>(pos - data_.GetAddress());
        assert(index < size_);

        for (size_t i = index; i + 1 < size_; ++i) {
            data_[i] = std::move(data_[i + 1]);
        }
        PopBack();
        return data_.GetAddress() + index;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

private:
    static constexpr bool kUseMove = std::is_nothrow_move_constructible_v<T> ||
        !std::is_copy_constructible_v<T>;

    static void Relocate(T* src, size_t count, T* dst) {
        if (count == 0) {
            return;
        }
        if constexpr (kUseMove) {
            std::uninitialized_move_n(src, count, dst);
        } else {
            std::uninitialized_copy_n(src, count, dst);
        }
    }

    size_t NextCapacity() const noexcept {
        return Capacity() == 0 ? 1 : Capacity() * 2;
    }

    void AssignFrom(const Vector& rhs) {
        size_t i = 0;

        for (; i < size_ && i < rhs.size_; ++i) {
            data_[i] = rhs.data_[i];
        }

        if (i < rhs.size_) {
            std::uninitialized_copy_n(rhs.data_.GetAddress() + i, rhs.size_ - i,
                data_.GetAddress() + i);
        } else {
            std::destroy_n(data_.GetAddress() + i, size_ - i);
        }
        size_ = rhs.size_;
    }

    void DestroyElements() noexcept {
        if (size_ != 0) {
            std::destroy_n(data_.GetAddress(), size_);
        }
    }

    template <typename... Args>
    iterator EmplaceInPlace(size_t index, Args&&... args) {
        T value(std::forward<Args>(args)...);

        T* last = data_.GetAddress() + size_;
        new (static_cast<void*>(last)) T(std::move(data_[size_ - 1]));
        try {
            for (size_t i = size_ - 1; i > index; --i) {
                data_[i] = std::move(data_[i - 1]);
            }

            data_[index] = std::move(value);
        } catch (...) {
            std::destroy_at(last);
            throw;
        }
        ++size_;
        return data_.GetAddress() + index;
    }

    template <typename... Args>
    iterator EmplaceWithRealloc(size_t index, Args&&... args) {
        const size_t new_capacity = NextCapacity();
        RawMemory<T> new_data(new_capacity);

        Relocate(data_.GetAddress(), index, new_data.GetAddress());

        T* new_elem = new_data.GetAddress() + index;
        try {
            new (static_cast<void*>(new_elem)) T(std::forward<Args>(args)...);
        } catch (...) {
            std::destroy_n(new_data.GetAddress(), index);
            throw;
        }

        try {
            Relocate(data_.GetAddress() + index, size_ - index,
                new_data.GetAddress() + index + 1);
        } catch (...) {
            std::destroy_n(new_data.GetAddress(), index + 1);
            throw;
        }

        DestroyElements();
        data_.Swap(new_data);
        ++size_;
        return data_.GetAddress() + index;
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};