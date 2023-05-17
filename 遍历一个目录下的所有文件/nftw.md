
> nftw()函数是对执行类似功能的老函数 ftw()的加强。由于提供了更多功能，对符号链接的处理也更易于把握
> 
> GNU C 语言函数库也提供了派生自 BSD 分支的 fts API(fts_open()、fts_read()、fts_children()、fts_set()和 fts_close())。这些函数执行的任务类似于 ftw()和 nftw()，但在遍历树方面为应用程序提供了更大的灵活性。


```c
NAME
       ftw, nftw - file tree walk  文件树遍历

#include <ftw.h>
int nftw(const char *dirpath, 
         int (*fn)(const char *fpath, const struct stat *sb, int typeflag, struct FIW *ftwbuf),
         int nopenfd,
         int flags);

int ftw(const char *dirpath,
        int (*fn)(const char *fpath, const struct stat *sb, int typeflag),
        int nopenfd);
```

nftw()函数遍历由 dirpath 指定的目录树，并为目录树中的每个文件调用一次由程序员定义的 fn 函数。

- 默认情况下，nftw()会针对给定的树执行未排序的前序遍历，即对各目录的处理要先于各目录下的文件和子目录
- 当nftw()遍历目录树时，最多会为树的每一层打开一个文件描述符。
- 参数nopenfd指定了nftw()可以使用文件描述符数量的最大值
  - 如果目录树深度超过这一最大值，那么nftw()会在做好记录的前提下，关闭并重新打开描述符，从而避免同时持有的描述符数目突破上限nopenfd(从而导致运行越来越慢)
  - 在较老的 UNIX 实现中，有的系统要求每个进程可打开的文件描述符数量不得超过 20 个，这更突显出这一参数的必要性。现代 UNIX 实现允许进程打开大量的文件描述符，因此，在指定该数目时出手可以大方一些（比如，10 或者更多）
- nftw()的 flags 参数由 0 个或多个下列常量相或(|)组成，这些常量可对函数的操作做出修正
  - FTW_CHDIR
    - 在处理目录内容之前先调用 chdir()进入每个目录
    - 如果打算让 func 在 pathname 参数所指定文件的驻留目录下展开某些工作，那么就应当使用这一标志
  - FTW_DEPTH
    - 对目录树执行后序遍历。这意味着，nftw()会在对目录本身执行 func 之前先对目录中的所有文件（及子目录）执行 func 调用
    - （这一标志名称容易引起误会—nftw()遍历目录树遵循的是深度优先原则，而非广度优先。而这一标志的作用其实就是将先序遍历改为后序遍历
  - FTW_MOUNT
    - 不会越界进入另一文件系统
    - 因此，如果树中某一子目录是挂载点，那么不会对其进行遍历
  - FTW_PHYS
    - 默认情况下，nftw()对符号链接进行解引用操作。而使用该标志则告知 nftw()函数不要这么做。相反，函数会将符号链接传递给 func 函数，并将 typeflag 值置为 FTW_SL

nftw()为每个文件调用 fn 时传递 4 个参数:

- 第一个参数 pathname 是文件的路径名。这个路径名可以是绝对路径，也可以是相对路径
  - 如果指定 dirpath 时使用的是绝对路径，那么pathname 就可能是绝对路径
  - 反之，如果指定 dirpath 时使用的是相对路径名，则 pathname中的路径可能是相对于进程调用 ntfw()时的当前工作目录而言
- 第二个参数 statbuf 是一枚指针，指向 stat 结构，内含该文件的相关信息
- 第三个参数 typeflag 提供了有关该文件的深入信息，并具有如下特征值之一
  - FTW_D ：这是一个目录
  - FTW_DNR ：这是一个不能读取的目录（所以 nftw()不能遍历其后代）
  - FTW_DP ：正在对一个目录进行后序遍历，当前项是一个目录，其所包含的文件和子目录已经处理完毕
  - FTW_F ：该文件的类型是除目录和符号链接以外的任何类型
  - FTW_NS ：对该文件调用 stat()失败，可能是因为权限限制。Statbuf 中的值未定义
  - FTW_SL ：这是一个符号链接。仅当使用
  - FTW_PHYS ：标志调用 nftw()函数时才返回该值
  - FTW_SLN ：这是一个悬空的符号链接。仅当未在flags参数中指定FTW_PHYS标志时才会出现该值
- Func 的第四个参数 ftwbuf 是一枚指针，所指向结构定义如下:
  - 该结构的 base 字段是指 func 函数中 pathname 参数内文件名部分（最后一个“/”字符之后的部分）的整型偏移量
  - level 字段是指该条目相对于遍历起点（其 level 为 0）的深度


```c
struct FTW {
	int base;
	int level;
};
```

每次调用 func 都必须返回一个整型值，由 nftw()加以解释

- 如果返回 0，nftw()会继续对树进行遍历，如果所有对 func 的调用均返回 0，那么 nftw()本身也将返回 0 给调用者
- 若返回非 0 值，则通知 nftw()立即停止对树的遍历，这时nftw()也会返回相同的非 0 值

由于 nftw()使用的数据结构是动态分配的，故而应用程序提前终止目录树遍历的唯一方法就是让 func 调用返回一个非 0 值