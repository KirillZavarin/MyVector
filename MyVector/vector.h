#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>

template<typename T>
class RawMemory {
public:

    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept 
    : buffer_(other.buffer_)
    , capacity_(other.capacity_) {
        other.buffer_ = nullptr;
        other.capacity_ = 0;
    }

    RawMemory& operator=(RawMemory&& other) noexcept {
        std::swap(buffer_, other.buffer_);
        capacity_ = other.capacity_;
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
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
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T * buf) noexcept {
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

    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        return data_ + size_;
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        return data_ + size_;
    }
    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator cend() const noexcept {
        return data_ + size_;
    }

    Vector()
    : data_() {
        size_ = 0;
    }

    explicit Vector(size_t size) 
    : data_(size)
    , size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
    : data_(other.size_)
    , size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept
    : data_(std::move(other.data_)) {
        size_ = std::exchange(other.size_, 0);
    }

    Vector& operator=(const Vector& other) {
        if (this != &other) {
            if (other.size_ > data_.Capacity()) {
                Vector other_copy(other);
                Swap(other_copy);
            }
            else {
                size_t min_size = std::min(other.size_, size_);
                std::copy_n(other.data_.GetAddress(), min_size, data_.GetAddress());
                if (other.size_ < size_) {
                    DestroyN(data_.GetAddress() + min_size, size_ - min_size);
                }
                else {
                    std::uninitialized_copy_n(other.data_.GetAddress() + min_size, (other.size_ - size_), data_.GetAddress() + min_size);
                }
            }
        }
        size_ = other.size_;
        return *this;
    }

    Vector& operator=(Vector&& other) {
        Swap(other);
        return *this;
    }

    void Resize(size_t new_size) {
        if (new_size > size_) {
            if (new_size > data_.Capacity()) {
                Reserve(new_size);
            }
            std::uninitialized_default_construct_n(data_ + size_, new_size - size_);
            size_ = new_size;
        }
        else {
            DestroyN(data_.GetAddress() + new_size, size_ - new_size);
            size_ = new_size;
        }
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }
       
    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }
    
    template <typename... Types>
    T& EmplaceBack(Types&&... args) {
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data + size_) T(std::forward<Types>(args)...);
            this->MoveOrCopyUninitialized(data_.GetAddress(), size_, new_data.GetAddress());
            DestroyN(data_.GetAddress(), size_);
            data_.Swap(new_data);
            size_++;
            return data_[size_ - 1];
        }
        new (data_ + size_) T(std::forward<Types>(args)...);
        size_++;
        return data_[size_ - 1];
    }

    void PopBack() {
        if (Size() != 0) {
            Destroy(data_ + (size_ - 1));
            size_--;
        }
    }

    template <typename... Types> 
    iterator Emplace(const_iterator pos, Types&&... args) {
        if (pos == end()) {
            this->EmplaceBack(std::move(args)...);
            return end() - 1;
        }

        int pos_index = pos - cbegin();
        if (size_ == Capacity()) {
            InsertionWithRelocation(pos_index, args);
        }
        else {
            InsertionWithoutRelocation(pos_index, args);
        }
        size_++;
        return begin() + pos_index;
    }

    iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/ {
        size_t pos_index = pos - cbegin();
        std::move(begin() + pos_index + 1, end(), begin() + pos_index);
        this->PopBack();
        return begin() + pos_index;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return this->Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return this->Emplace(pos, std::move(value));
    }

    void Reserve(size_t new_capacity) {

        if (new_capacity <= Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);

        MoveOrCopyUninitialized(data_.GetAddress(), size_, new_data.GetAddress());

        DestroyN(data_.GetAddress(), size_);

        data_.Swap(new_data);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    ~Vector() {
        DestroyN(data_.GetAddress(), size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:

    static void Destroy(T* buf) noexcept {
        buf->~T();
    }

    static void DestroyN(T* buf, size_t n) noexcept {
        for (size_t i = 0; i < n; i++) {
            Destroy(buf + i);
        }
    }

    static void CopyConstruct(T* buf, const T& elem) {
        new (buf) T(elem);
    }

    template <typename... Types>
    void InsertionWithRelocation(int pos_index, Types&&... args) {
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);

        new (new_data + pos_index) T(std::forward<Types>(args)...);

        try {
            MoveOrCopyUninitialized(data_.GetAddress(), pos_index, new_data.GetAddress());
        }
        catch (...) {
            Destroy(data_ + pos_index);
            throw;
        }

        try {
            MoveOrCopyUninitialized(data_ + pos_index, size_ - pos_index, new_data + pos_index + 1);
        }
        catch (...) {
            DestroyN(new_data.GetAddress(), pos_index);
            throw;
        }
        DestroyN(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    template <typename... Types>
    void InsertionWithoutRelocation(int pos_index, Types&&... args) {
        new (end()) T(std::forward<T>(*(end() - 1)));
        std::move_backward(begin() + pos_index, end() - 1, end());
        data_[pos_index] = T(std::forward<Types>(args)...);
    }

    void MoveOrCopyUninitialized(T* data, int pos_index, T* new_data) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data, pos_index, new_data);
        }
        else {
            std::uninitialized_copy_n(data, pos_index, new_data);
        }
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};