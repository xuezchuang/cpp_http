title: stl
author: xuezc
tags:
  - std
  - c++
description: 对标准库的深入学习
categories:
  - c++
abbrlink: a6a19e9a
date: 2023-04-02 10:34:00
---
# 深入stl Iterator
## 迭代器的数据结构
容器本身指向_Container_proxy

```c++
struct _Container_base12;
struct _Iterator_base12;
#容器指针
struct _Container_proxy
{	
    #store head of iterator chain and back pointer
    const _Container_base12 *_Mycont;
    _Iterator_base12 *_Myfirstiter;
    
};
#容器结构体
struct _Iterator_base12
{       
    _Container_proxy *_Myproxy;
    _Iterator_base12 *_Mynextiter;
}
#容器
struct _Container_base12
{       
    _Container_proxy *_Myproxy;
}
```


# Alloc的初始化,全部以Deque容器为例
Alloc的初始化会初始化容器_Container_base12


## _Deque_alloc分配符
```c++
template<class _Val_types>
struct _Deque_val: public _Container_base12
{
    typedef typename _Val_types::_Mapptr _Mapptr;
    
    _Mapptr _Map;       // pointer to array of pointers to blocks
    size_type _Mapsize;	// size of map array, zero or 2^N
    size_type _Myoff;	// offset of initial element
    size_type _Mysize;	// current length of sequence

    
};
#_Deque_alloc的分配符
template<bool _Al_has_storage,class _Alloc_types>
class _Deque_alloc : public _Deque_val<typename _Alloc_types::_Val_types>
{
}
#_Deque_alloc的模板特例化分配符
template<false,class _Alloc_types>
class _Deque_alloc : public _Deque_val<typename _Alloc_types::_Val_types>
{
}
struct _Container_proxy
{	
    #store head of iterator chain and back pointer
    const _Container_base12 *_Mycont;
    _Iterator_base12 *_Myfirstiter;
    
};
```

## allocator分配符结构
_Allocator_base
```c++
template<class _Ty>
struct _Allocator_base
{	
    typedef _Ty value_type;
};
#模板特化
template<class _Ty>
struct _Allocator_base<const _Ty>
{	
    typedef _Ty value_type;
};
```
allocator
```c++
template<class _Ty>
class allocator: public _Allocator_base<_Ty>
{	
    typedef _Ty value_type;
};
```

# 容器的实现
不指定Alloc的情况下,基类都是_Deque_alloc<false,class _Alloc_types>

```c++
template<class _Ty,class _Alloc = allocator<_Ty> >
class allocator: 
    public _Deque_alloc<<!is_empty<_Alloc>::value,_Deque_base_types<_Ty, _Alloc>>
{	
    typedef _Ty value_type;
};
```

```c++
template<class _Ty>
struct _Deque_simple_types
{	
    typedef _Alloc0 _Alloc;
    typedef _Deque_base_types<_Ty, _Alloc> _Myt;
    #如果是基础类型,返回_Deque_simple_types<>,否则返回自定义结构体    
    typedef typename _If<_Is_simple_alloc<_Alty>::value,
		_Deque_simple_types<typename _Alty::value_type>,
		_Deque_iter_types<typename _Alty::value_type,
			typename _Alty::size_type,
			typename _Alty::difference_type,
			typename _Alty::pointer,
			typename _Alty::const_pointer,
			typename _Alty::reference,
			typename _Alty::const_reference,
			_Mapptr> >::type
		_Val_types;
};
```
_Deque_base_types
```c++
template<class _Ty,class _Alloc0>
struct _Deque_base_types    
{	
    typedef _Alloc0 _Alloc;
    typedef _Deque_base_types<_Ty, _Alloc> _Myt;
    #如果是基础类型,返回_Deque_simple_types<>,否则返回自定义结构体    
    typedef typename _If<_Is_simple_alloc<_Alty>::value,
		_Deque_simple_types<typename _Alty::value_type>,
		_Deque_iter_types<typename _Alty::value_type,
			typename _Alty::size_type,
			typename _Alty::difference_type,
			typename _Alty::pointer,
			typename _Alty::const_pointer,
			typename _Alty::reference,
			typename _Alty::const_reference,
			_Mapptr> >::type
		_Val_types;
};
```
# aligned_storage
并不是说“它只适用于POD类型”。事实上，使用alignd_storage的一种常用方法是将其作为一个内存块，在这个内存块中可以手动创建和销毁任何类型的其他对象
```c++
#include <iostream>
#include <new>
#include <string>
#include <type_traits>
 
template<class T, std::size_t N>
class static_vector
{
    // properly aligned uninitialized storage for N T's
    std::aligned_storage_t<sizeof(T), alignof(T)> data[N];
    std::size_t m_size = 0;
 
public:
    // Create an object in aligned storage
    template<typename ...Args> void emplace_back(Args&&... args)
    {
        if( m_size >= N ) // possible error handling
            throw std::bad_alloc{};
 
        // construct value in memory of aligned storage
        // using inplace operator new
        ::new(&data[m_size]) T(std::forward<Args>(args)...);
        ++m_size;
    }
 
    // Access an object in aligned storage
    const T& operator[](std::size_t pos) const
    {
        // Note: std::launder is needed after the change of object model in P0137R1
        return *std::launder(reinterpret_cast<const T*>(&data[pos]));
    }
 
    // Destroy objects from aligned storage
    ~static_vector()
    {
        for(std::size_t pos = 0; pos < m_size; ++pos)
            // Note: std::launder is needed after the change of object model in P0137R1
            std::destroy_at(std::launder(reinterpret_cast<T*>(&data[pos])));
    }
};
 
int main()
{
    static_vector<std::string, 10> v1;
    v1.emplace_back(5, '*');
    v1.emplace_back(10, '*');
    std::cout << v1[0] << '\n' << v1[1] << '\n';
}
```
## alignof可以得到结构体的对齐大小

# std::forward
## 背景
```c++
class CData
{
public:
	CData() = delete;
	CData(const char* ch) : data(ch)
	{
		std::cout << "CData(const char* ch)" << std::endl;
	}
	CData(const std::string& str) :data(std::move(str))
	{
		std::cout << "CData(const std::string& str)" << std::endl;
	}
	CData(std::string&& str) : data(std::move(str))// data(std::forward<std::string>(str))
	{
		std::cout << "CData(std::string&& str)" << std::endl;
	}
	~CData()
	{
		std::cout << "~CData()" << std::endl;
	}
private:
	std::string data;
};

template<typename T>
CData* Creator(T&& t)
{
	return new CData(t);// std::forward<T>(t));
	//return new CData(std::forward<T>(t));
}
void Test__forward()
{
	{
		const char* value = "hello";
		std::string str1 = "hello";
		std::string str2 = " world";
		//CData* p = Creator(value);
		//CData* p = Creator(str1);
		CData* p = Creator(str1 + str2);

		delete p;
	}
	int a = 0;
}
```
运行效果
```c++
CData(std::string& str)
~CData()
```
在模板中,传递参数改为std::forward<T>(t),效果:  
 
```c++
CData(std::string& str)
~CData()
```

这里如果需要高效率，对于右值的调用应该使用CData(std::string&& str) : data(str)移动函数操作。
在模板Creator中,不适用std::forward的话T&&会退化为T&

## 结论
std::forward会将输入的参数原封不动地传递到下一个函数中，这个“原封不动”指的是，如果输入的参数是左值，那么传递给下一个函数的参数的也是左值；如果输入的参数是右值，那么传递给下一个函数的参数的也是右值。
定义:
```c++
template<class _Ty>
_NODISCARD constexpr _Ty&& forward(remove_reference_t<_Ty>& _Arg) noexcept
{	// forward an lvalue as either an lvalue or an rvalue
	return (static_cast<_Ty&&>(_Arg));
}

CData* Creator(T&& t)
{
	return new CData(std::forward<T>(t));
}

```
完美转发使用两步来完成任务：
```
在模板中使用&&接收参数。
使用std::forward()转发给被调函数.
```
# map研究
研究几个常用的哈希map
## 哈希表基本设计
### 碰撞处理
不同的key经哈希函数映射到同一个桶，称作哈希碰撞。各种实现中最常见的碰撞处理机制是链地址法（chaining）和开放寻址法（open-addressing）。
### 链地址法
在哈希表中，每个桶存储一个链表，把相同哈希值的不同元素放在链表中。这是C++标准容器通常采用的方式。
```
优点：
实现最简单直观
空间浪费较少
```
### 开放寻址法
若插入时发生碰撞，从碰撞发生的那个哈希桶开始，按照一定的次序，找出一个空闲的桶。
```
优点：
每次插入或查找操作只有一次指针跳转，对CPU缓存更友好
所有数据存放在一块连续内存中，内存碎片更少
```
当max load factor较大时，性能不如链地址法。然而当我们主动牺牲内存，选择较小的max load factor时（例如0.5），形势就发生逆转，开放寻址法反而性能更好。因为这时哈希碰撞的概率大大减小，缓存友好的优势得以凸显。
### Max load factor
对链地址法哈希表，指平均每个桶所含元素个数上限。
对开放寻址法哈希表，指已填充的桶个数占总的桶个数的最大比值。
max load factor越小，哈希碰撞的概率越小，同时浪费的空间也越多。
### Growth factor
指当已填充的桶达到max load factor限定的上限，哈希表需要rehash时，内存扩张的倍数。growth factor越大，哈希表rehash的次数越少，但是内存浪费越多
### 空闲桶探测方法
在开放寻址法中，若哈希函数返回的桶已经被其他key占据，需要通过预设规则去临近的桶中寻找空闲桶。最常见有以下方法
```
线性探测（linear probing）
平方探测（quadratic probing）
双重哈希（double hashing）
```
线性探测法对比其他方法，平均需要探测的桶数量最多。但是线性探测法访问内存总是顺序连续访问，最为缓存友好。因此，在冲突概率不大的时候（max load factor较小），线性探测法是最快的方式。

## Mfc的CMap(链表)

## stl的hash_map 和unordered_map

## stl的红黑树map

## robin_map(开放寻址)