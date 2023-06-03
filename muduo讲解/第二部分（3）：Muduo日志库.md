<a name="BfDfr"></a>
## 日志库的作用
日志的作用：便于我们观察服务器当前状态，也有利于排查服务器上的错误，便于维护服务器<br />日志分为同步和异步日志：<br />同步日志：有日志信息产生的话立刻输出到终端或者文件

- 优点：可以马上观察到服务器的状态
- 缺点：如果日志过多，就会频繁写数据，写是一种IO操作，比较耗时，日志过多后，线程大部分时间都浪费在写日志上了，而耽误了网络IO的处理，也就限制了高并发。

异步日志：把日志先保存到缓存中，当日志达到一定数量后，或者缓冲区存满后再统一写入终端或者文件。

- 优点：这种积累日志的方式大大减少了日志IO操作的次数，几乎不耽误网络IO。
- 缺点：不能把日志实时写入文件，当意外断电，很可能缓存中大量日志还没来得及写入文件，导致大量日志丢失。
<a name="SxuW1"></a>
## 异步日志原理
异步日志是高并发服务器必不可少的，因此，下面我仅仅给出了muduo异步日志的大致原理图，同步日志其实与std::cout差别不大，就不画原理图了。<br />muduo的日志库分采用前后端分离。产生日志的线程都可以称作前端。有一个线程专门负责把前端的日志写入文件，这个线程就称为后端。<br />**muduo日志库采用双缓冲技术**，所谓双缓冲技术就是前后端各有一个（其实是各有两个）大小为4MB的Buffer，之所以各有一个Buffer，是为了当前端Buffer写满了之后，后端空闲的Buffer能够立刻和前端的Buffer互换，互换完成后就可以释放锁，让前端继续向互换后的Buffer中写入日志，而不必等待后端把日志全部写入文件后再释放锁，大大减小了锁的粒度，这也是muduo异步日志高效的关键原因之一。（详细请看P114，5.3节）
:::tips
需要说明的是：

- muduo前后端各有两个大小为4MB的Buffer，下图为了方面明白异步日志的思想，简化为前后端各自只有一个Buffer
- 这里的Buffer不是Buffer.h中定义的Buffer，而是FixedBuffer.h中定义的，可以理解为一个有4MB大小的字符数组
:::
现在的关键是，前后端的Buffer什么时候互换呢？

- 第一种情况：前端Buffer写满了，就会唤醒后台线程互换Buffer，并把Buffer中的日志写入文件
- 第二种情况：**为了及时把前端Buffer中的日志写入文件，日志库也会每3秒执行一次交换Buffer的操作**。(面试官可能会问你为什么要3秒交换一次Buffer)

下图就以第二种情况为例，展示日志库双缓冲原理。<br />![异步日志原理.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684481455106-f7ff2a96-5495-40bd-82e6-ec7c0fff64d8.svg#clientId=u172e38ba-db8c-4&from=paste&height=545&id=u0d53131f&originHeight=681&originWidth=696&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=34244&status=done&style=none&taskId=u03c6f494-21cb-460f-984d-b1107ed3441&title=&width=556.8)<br />在0~3秒之间，当前端的线程调用了`LOG_INFO << 日志信息;`，会开辟一个4KB大小的空间，用于存储“日志信息”，当该语句执行结束时，就已经把日志信息都存储到前端的Buffer（黄色Buffer）中，同时析构自己刚开辟的4KB的空间。<br />在3秒之后，后端线程被唤醒，为了让前端阻塞时间更短，后端把自己的红色Buffer和前端的黄色Buffer互换，然后就可以释放锁，这样前端又能及时的向红色Buffer中写入新的日志信息。后端线程也可以把来自前端的黄色Buffer中的数据写入文件。
<a name="ferif"></a>
## 各个类的作用
| 类名 | 功能 |
| --- | --- |
| FixedBuffer | 封装了一个固定长度的字符数组当作buffer，具有向buffer存取数据的功能。（官方源码中，该类定义在LogStream.h中） |
| LogStream | 此类为多种数据类型重载了`<<`运算符，实现了类似`cout`一样的功能，重载的`<<`函数内部都是将要`<<`的数据存储到它自己的成员变量`buffer_`中，这个`buffer_`是`FixedBuffer`类型，大小是4KB。 |
| FileUtil | 封装的工具类，可以看作是个封装过的文件指针。具有将`char*`指向的数据写入文件的功能，其类也有个64KB的缓冲区供写文件时候使用。 |
| LogFile | 具有利用`FileUtil`把日志写入文件的功能，也可以根据时间和日志文件大小创建新日志文件的功能。 |
| AsyncLogging | 前端有两个4M的大缓冲区（`FixedBuffer`类型）用于存储日志信息，后端有个子线程，也有两个4M的缓冲区，用于和前端写了日志的缓冲区进行交换，交换后，后端就借助`LogFile`可以把日志信息往文件中写入。 |
| Logging | 能够设置日志级别，能够设定`LogStream`获得的日志信息输出到什么位置（输出到stdout还是文件） |

<a name="duajv"></a>
## 同步日志流程
<a name="mLSse"></a>
### 产生日志
下面以前端调用`LOG_INFO << "abcd" << 15;`为例，看看到底内部发生了什么。
```cpp
#define LOG_INFO if (logLevel() <= Logger::INFO) \
	Logger(__FILE__, __LINE__).stream()
```
首先从上面`LOG_INFO`的宏定义可见，`LOG_INFO`实际上是创建了一个`Logger`临时对象，并调用其成员函数`Stream()`，`Stream()`返回的是一个`LogStream`对象的引用，`LogStream`和`cout`很像， 该类重载了`<<`的多种形式，以便`LOG_INFO`能够`<<`多种类型数据。`LogStream`有一个4KB的`buffer_`，其所有重`<<`载的函数都只做一件事：把日志信息（此例中为`“abcd”`和`15`），添加（`apend`）到`LogStream`的`buffer_`中。由此也可见`**LogStream**`**类的**`**buffer_**`**尽管有4KB大小，但是只存储一条日志信息。**
<a name="K4f8a"></a>
### 日志输出到指定位置
<a name="V8rgM"></a>
#### 日志输出到`stdout`
怎么把在`LogStream`类的`buffer_`中存储的的一条日志写输出到指定位置（以`stdout`为例）呢？<br />从上面`LOG_INFO`的宏定义可见，`LOG_INFO << "abcd" << 15;`执行结束后就会析构`Logger`对象，`buffer_`中的数据也正是`Logger`析构的时候被输出到`stdout`，`Logger`析构函数会调用`defaultOutput`函数（定义在`Logging.cc`中），改函数代码如下：
```cpp
static void defaultOutput(const char* data, int len)
{
    fwrite(data, len, sizeof(char), stdout);
}
```
其中，`data`就是`LogStream`的`buffer_`中存储的数据。
<a name="Tnt7A"></a>
#### 日志输出到文件
如果同步日志想把日志信息fwrite到文件中，需要手动完成以下步骤：<br />接下来讲解会涉及到`FileUtil`中的`buffer_`(64KB大小，是`FileUtil`文件指针`fp_`对应的缓冲区)，为了区分`LogStream`中的`buffer_`（4KB大小，只存储一条日志），我们在`buffer_`上加上类名。

1. 创建一个`LogFile`对象。`LogFile`类有个成员函数`apend(char *data, int len)`，只要给他日志信息，即给他`LogStream::buffer_`中的`char *`数据，他就能把日志信息写入文件。所以需要先创建有个`LogFile`对象，才能写文件。
2. 让`Logger`类的输出位置指向文件。由于`Logger`默认输出位置是`stdout`，所以需要改变`Logger`的输出位置。由于`Logger`类提供了`Logger::setOutput(OutputFunc out)`接口，因此可以通过如下方式将日志输出位置改为文件：
```cpp
#include "Logging.h"
#include "LogFile.h"

std::unique_ptr<LogFile> g_logFile;
// msg就是LogStream::buffer_中存储的一条日志信息，现在需要把这条日志信息apend到FileUtil的buffer_中
void dummyOutput(const char* msg, int len)
{	
	if (g_logFile) {
    	g_logFile->append(msg, len);
    }
}

int main() {
	g_logFile.reset(new LogFile("test_log_st", 500*1000*1000, false));
    Logger::setOutput(dummyOutput);		// 改变Logger的输出位置
    LOG_INFO << "Hello 0123456789" << " abcdefghijklmnopqrstuvwxyz";
    return 0;
}
```
此后，`LOG_INFO << data`创建的临时`Logger`对象在析构时，`Logger`类的`g_output`（可理解为函数指针）就不再指向`defaultOutput`函数而是指向`dummyOutput`函数了，即，就不会调用`defaultOutput`输出到`stdout`上了，而是调用上述代码的`dummyOutput`函数，把日志`LogStream::buffer_`中存储的一条日志通过`Logfile::apend()`函数写入到文件中。`Logging`析构函数核心代码如下：
```cpp
Logger::~Logger()
{
    ...省略代码...
	// 得到LogStream的buffer_
    const LogStream::SmallBuffer& buf(stream().buffer());
    // 输出(默认向终端输出)
    g_output(buf.data(), buf.length());
    // FATAL情况终止程序
    if (impl_.level_ == FATAL)
    {
        g_flush();  // 把缓冲区的数据强制写入指定的位置（默认是写入stdout）
        abort();
    }
}

void Logger::setOutput(OutputFunc out)  
{
    g_output = out;
}
```
<a name="QTgDR"></a>
## 异步日志流程
异步日志前端产生日志和同步是一致的，下面主要说明异步情况下，`LogStream::buffer_`中的数据是怎么输出到指定位置的。由于异步日志不是默认的方式，需要自行开启，下面就从开启异步日志开始讲解。
<a name="X6Yob"></a>
### 开启异步日志
`AsycnLogging`只提供了向文件写入的功能，由于`Logger`默认输出位置是`stdout`，所以需要像同步日志那样调用`Logger::setOutput(OutputFunc out)`接口，把日志输出位置改为文件。核心代码如下：
```cpp
AsyncLogging* g_asyncLog = nullptr;
void asyncOutput(const char* msg, int len)
{
	g_asyncLog->append(msg, len);
}

int main()
{
    off_t kRollSize = 500*1000*1000;
    AsyncLogging log(“basename”, kRollSize);
    log.start();		// 启动异步日志的后台线程
    g_asyncLog = &log;
    Logger::setOutput(asyncOutput);
    LOG_INFO << "Hello 0123456789" << " abcdefghijklmnopqrstuvwxyz";
    return 0;
}
```
<a name="YL97Z"></a>
### 把日志写入缓冲区
经过同步日志分析，我们直到`LOG_INFO << data`执行完成时，就会调用`asyncOutput`，接下来我们看一下日志信息是怎么写到用户自己定义的缓冲区的（即`g_asyncLog->append(msg, len);`做了什么事）：<br />`AsyncLogging::append`函数基本原理是：先将日志信息存储到用户自己开辟的大缓冲区，当缓冲区写满后，通知后台线程把缓冲区的数据写入文件。`apend`代码如下：注意`currentBuffer_`有4M大小，之所以很大，是为了多积累一些日志信息，然后一并交给后端写入文件，避免频繁通知后台写文件。另外，`currentBuffer_->append()`是单纯的把日志信息写入`currentBuffer_`中，而不是像`LogFile`的`apend()`是将数据写入文件中。
```cpp
// 前端如果开启异步日志的话，就会调用这个，把日志信息写入currentBuffer_中
void AsyncLogging::append(const char* logline, int len)
{
    // apend会在多个线程中调用，也就是多个线程会同时像currentBuffer_中写数据，因此要加锁
    std::lock_guard<std::mutex> lock(mutex_);
    // 缓冲区剩余空间足够则直接写入
    if (currentBuffer_->avail() > len)
    {
        currentBuffer_->append(logline, len);
    }
    else
    {
        // 当前缓冲区空间不够，将新信息写入备用缓冲区
        // 将写满的currentBuffer_装入vector中，即buffers_中，注意currentBuffer_是独占指针，
        // move后自己就指向了nullptr
        buffers_.push_back(std::move(currentBuffer_));
        // nextBuffer_不空，说明nextBuffer_指向的内存还没有被currentBuffer_抢走，也就是
        // nextBuffer_还没开始使用
        if (nextBuffer_) 
        {
            // 把nextBuffer_指向的内存区域交给currentBuffer_，nextBuffer_也被置空
            currentBuffer_ = std::move(nextBuffer_);
        } 
        else 
        {
            // 备用缓冲区也不够时，重新分配缓冲区，这种情况很少见
            currentBuffer_.reset(new Buffer);
        }
        currentBuffer_->append(logline, len);
        // 唤醒写入磁盘得后端线程
        cond_.notify_one();
    }
}
```
<a name="mMD1b"></a>
### 把前端日志写入文件
后端负责把前端积累的日志拿过来，然后写入文件中，核心代码如下：
```cpp
void AsyncLogging::threadFunc()
{
    // output有写入磁盘的接口，异步日志后端的日志信息只会来自buffersToWrite，也就是说不会
    // 来自其他线程，因此不必考虑线程安全，所以output的threadSafe设置为false
    LogFile output(basename_, rollSize_, false);    
    // 后端缓冲区，用于归还前端得缓冲区，currentBuffer nextBuffer
    BufferPtr newBuffer1(new Buffer);
    BufferPtr newBuffer2(new Buffer);
    newBuffer1->bzero();
    newBuffer2->bzero();
    // 缓冲区数组置为16个，用于和前端缓冲区数组进行交换
    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);
    while (running_)
    {
        {
            // 互斥锁保护，这样别的线程在这段时间就无法向前端Buffer数组写入数据
            std::unique_lock<std::mutex> lock(mutex_);
            if (buffers_.empty())
            {
                // 等待三秒也会解除阻塞
                cond_.wait_for(lock, std::chrono::seconds(3));
            }
            // 此时正使用得buffer也放入buffer数组中（没写完也放进去，避免等待太久才刷新一次）
            buffers_.push_back(std::move(currentBuffer_));
            // 归还正使用缓冲区
            currentBuffer_ = std::move(newBuffer1);
            // 后端缓冲区和前端缓冲区交换
            buffersToWrite.swap(buffers_);
            if (!nextBuffer_)
            {
                nextBuffer_ = std::move(newBuffer2);
            }
        }
        ...省略归还缓冲区的代码...
        // 遍历所有 buffer，将其写入文件
        for (const auto& buffer : buffersToWrite)
        {
            output.append(buffer->data(), buffer->length());
        }

        output.flush(); //清空文件缓冲区
    }
    output.flush();
}
```
`buffersToWrite`用于接管前端的所有日志信息，这样可以提前释放锁，以便前端可以继续写日志，`buffersToWrite`接管后，在慢慢的往文件中写，写入文件的核心代码是`output.append(buffer->data(), buffer->length());`
<a name="xOoWC"></a>
## 疑问解答
<a name="TkPUb"></a>
### `FileUtil`为什么要开辟一个64KB的`buffer_`?
`FileUtil`的作用是把给定的数据写入到指定的文件中，回答这个问题首先需要了解IO缓冲区。
<a name="geVjF"></a>
#### IO工作流程
当我们向文件指针`fp_`指向的文件写入数据时，数据并不是立即被写入到文件，而是系统提供了一个缓冲区（这个缓冲区我们不可见，当然我们也可以通过`setbuffer`来为`fp_`指定缓冲区），当我们调用`fwrite`时，数据先被写入缓冲区，当缓冲区满后，才会把数据写入文件。另外，如果不希望缓冲区写满后再写入文件，可以调用`fflush`刷新缓冲区，即把缓冲区的数据立即写入到文件中。
<a name="VmQhG"></a>
#### `FileUtil`为什么要开辟一个64KB的`buffer_`?
系统提供的缓冲区可能比较小，如果日志很多且密集，那缓冲区就很容易写满，也就会很频繁的向文件中写入数据，IO是比较耗时的，如果频繁向文件写入数据那时间都花在写文件上了，而没时间去处理业务了，所以我们可以手动为`FileUtil`类的文件指针`fp_`指定一个比较大的缓冲区，这样当日志很密集时，也不会很频繁的写入文件。<br />注意`FileUtil`的`apend`函数尽管通过循环不断向文件指针`fp_`指向的文件写数据，但不代表数据一定被写入到文件了，因为我们在`FileUtil`构造函数中为`fp_`指定了一块很大的缓冲区（64KB），`apend`尽管通过循环调用`write`一直向`fp_`指向的文件写数据，直到把给的`data`写完为止时，很可能缓冲区还是没写满，因此，不会立马写入到文件，而是暂时缓存在我们指定的缓冲区中（看起来像是数据全部写入文件了），当我们调用`flush`或者`write`时缓冲区满了，那就会立即写入文件，不过这些都是`wirte`函数调用的`fwrite`帮我们处理的事，不需要我们管，我们只需要不断调用`fwrite`，感觉数据全部写入了文件就行，至于数据是暂时放在缓冲区还是写入文件，就由`fwrite`替我们操心去吧。
```cpp
void FileUtil::append(const char* data, size_t len)
{
    // 记录已经写入的数据大小
    size_t written = 0;
    while (written != len)                          	// 只要没写完就一直写
        {
            // 还需写入的数据大小
            size_t remain = len - written;
            size_t n = write(data + written, remain);   // 返回成功写如入了n字节
            if (n != remain)                            // 剩下的没有写完就看下是否出错，没出错就继续写
            {
                int err = ferror(fp_);
                if (err)
                {
                    fprintf(stderr, "FileUtil::append() failed %s\n", getErrnoMsg(err));
                }
            }
            // 更新写入的数据大小
            written += n;
        }
    // 记录目前为止写入的数据大小，超过限制会滚动日志(滚动日志在LogFile中实现)
    writtenBytes_ += written;
}
```
<a name="trk8s"></a>
### LogFile类的append函数和flush函数什么时候需要加锁，什么时候不需要？
同步的时候需要，因为多个线程都可能产生日志，即`LOG_INFO << something`，他们都同时要写入同一个文件，所以需要加锁，异步的时候不需要，具体来说，产生日志的线程有很多，他们在写入用户开辟的大缓冲区时确实需要加锁，但是这只是写入缓冲区，并不是写入文件，当大缓冲区被写满后，异步日志的后端就会接管这个写满的大缓冲区，然后把缓冲区的日志通过`LogFile`写入到文件中，也就是说，异步日志开启的情况下，写文件的永远之后异步日志的后台线程，根本不存在竞争，所以不需要加锁。
```cpp
void LogFile::append(const char* logline, int len)
{
    if (mutex_)		//同步日志需要加锁
    {
        std::unique_lock<std::mutex> lock(*mutex_);
        append_unlocked(logline, len);
    }
    else			// 异步日志不需要加锁
    {
    	append_unlocked(logline, len);
    }
}

void LogFile::flush()
{
    if (mutex_)		//同步日志需要加锁
    {
        std::unique_lock<std::mutex> lock(*mutex_);
        file_->flush();
    }
    else			// 异步日志不需要加锁
    {
    	file_->flush();
    }
}
```
