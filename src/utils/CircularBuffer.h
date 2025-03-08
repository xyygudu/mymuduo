#include <iostream>
#include <vector>
#include <stdexcept>

template<typename T>
class CircularBuffer {
public:
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = T*;
        using reference = T&;

        iterator(CircularBuffer* buffer, size_t pos) : buffer_(buffer), pos_(pos) {}

        reference operator*() const { return buffer_->buffer_[pos_]; }
        pointer operator->() const { return &buffer_->buffer_[pos_]; }

        iterator& operator++() {
            pos_ = buffer_->next(pos_);
            if (pos_ == buffer_->tail_ || buffer_->empty()) {
                buffer_ = nullptr;
                pos_ = 0;
            }
            return *this;
        }

        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const iterator& a, const iterator& b) {
            return a.buffer_ == b.buffer_ && a.pos_ == b.pos_;
        };
        friend bool operator!=(const iterator& a, const iterator& b) {
            return !(a == b);
        };

    private:
        CircularBuffer* buffer_;
        size_t pos_;
    };

    explicit CircularBuffer(size_t capacity) : capacity_(capacity), size_(0), head_(0), tail_(0) {
        buffer_.resize(capacity);
    }

    void push_back(const T& item) {
        if (size_ == capacity_) {
            // 缓冲区已满，覆盖最旧的数据
            buffer_[tail_] = item;
            tail_ = next(tail_);
            head_ = tail_;
        }
        else {
            buffer_[tail_] = item;
            tail_ = next(tail_);
            ++size_;
        }
    }

    T pop_front() {
        if (empty()) {
            throw std::underflow_error("Buffer is empty");
        }
        T item = buffer_[head_];
        head_ = next(head_);
        --size_;
        return item;
    }

    T& front() {
        if (empty()) {
            throw std::underflow_error("Buffer is empty");
        }
        return buffer_[head_];
    }

    T& back() {
        if (empty()) {
            throw std::underflow_error("Buffer is empty");
        }
        return buffer_[prev(tail_)];
    }

    bool empty() const {
        return size_ == 0;
    }

    size_t size() const {
        return size_;
    }

    size_t capacity() const {
        return capacity_;
    }

    iterator begin() {
        if (empty()) return end();
        return iterator(this, head_);
    }

    iterator end() {
        return iterator(nullptr, 0);
    }

private:
    size_t next(size_t index) {
        return (index + 1) % capacity_;
    }

    size_t prev(size_t index) {
        return (index - 1 + capacity_) % capacity_;
    }

    std::vector<T> buffer_;  // 底层存储容器
    size_t capacity_;        // 缓冲区的最大容量
    size_t size_;            // 当前缓冲区中的元素数量
    size_t head_;            // 头部索引
    size_t tail_;            // 尾部索引
};


// 测试CircularBuffer
// int main() {
//     CircularBuffer<int> cb(5);  // 创建一个容量为5的循环缓冲区

//     // 插入一些元素
//     cb.push_back(1);
//     cb.push_back(2);
//     cb.push_back(3);
//     cb.push_back(4);
//     cb.push_back(5);

//     // 使用迭代器遍历缓冲区
//     for (auto it = cb.begin(); it != cb.end(); ++it) {
//         std::cout << *it << " ";
//     }
//     std::cout << std::endl;

//     // 使用范围 for 循环遍历缓冲区
//     for (const auto& item : cb) {
//         std::cout << item << " ";
//     }
//     std::cout << std::endl;

//     // 再次插入一些元素以覆盖之前的元素
//     cb.push_back(6);
//     cb.push_back(7);

//     // 使用迭代器遍历缓冲区
//     for (auto it = cb.begin(); it != cb.end(); ++it) {
//         std::cout << *it << " ";
//     }
//     std::cout << std::endl;

//     // 使用范围 for 循环遍历缓冲区
//     for (const auto& item : cb) {
//         std::cout << item << " ";
//     }
//     std::cout << std::endl;

//     return 0;
// }