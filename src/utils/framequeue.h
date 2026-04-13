#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

#include <QImage>
#include <atomic>

// 无锁环形队列（单生产者单消费者）
// 用于线程间传递 QImage 帧数据
//
// 设计：
//   - 固定大小数组，writeIdx 和 readIdx 各由一个线程独占修改
//   - 不需要 mutex，用 atomic 保证可见性
//   - 队列满时丢弃最旧帧（覆盖），保证实时性
template<int N>
class FrameQueue {
    QImage m_frames[N];
    std::atomic<int> m_writeIdx{0};
    std::atomic<int> m_readIdx{0};

public:
    // 生产者调用：写入一帧
    // 队列满时覆盖最旧帧（移动 readIdx），保证生产者不阻塞
    void push(const QImage &frame) {
        int w = m_writeIdx.load(std::memory_order_relaxed);
        int next = (w + 1) % N;

        // 队列满：移动 readIdx 丢弃最旧帧
        if (next == m_readIdx.load(std::memory_order_acquire)) {
            m_readIdx.store((m_readIdx.load(std::memory_order_relaxed) + 1) % N,
                            std::memory_order_release);
        }

        m_frames[w] = frame;
        m_writeIdx.store(next, std::memory_order_release);
    }

    // 消费者调用：读取一帧
    // 返回 false 表示队列空
    bool pop(QImage &frame) {
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire))
            return false;  // 队列空

        frame = m_frames[r];
        m_readIdx.store((r + 1) % N, std::memory_order_release);
        return true;
    }

    // 消费者调用：取最新帧，跳过中间所有帧
    // 用于主线程显示——只关心最新的处理结果
    bool latest(QImage &frame) {
        int r = m_readIdx.load(std::memory_order_relaxed);
        int w = m_writeIdx.load(std::memory_order_acquire);
        if (r == w) return false;  // 队列空

        // 跳到最新帧的位置
        int newest = (w - 1 + N) % N;
        frame = m_frames[newest];

        // 把 readIdx 移到 writeIdx，标记所有帧已消费
        m_readIdx.store(w, std::memory_order_release);
        return true;
    }

    // 查询队列中的帧数
    int size() const {
        int w = m_writeIdx.load(std::memory_order_relaxed);
        int r = m_readIdx.load(std::memory_order_relaxed);
        return (w - r + N) % N;
    }

    bool empty() const { return size() == 0; }
};

#endif // FRAMEQUEUE_H
