title: filament
author: xuezc
tags:
  - filament
categories: []
abbrlink: 9daa3ba6
description: filament
date: 2023-03-31 13:23:00
---
# 语法
## DriverAPI宏的展开
### 获取可变参数宏的长度
```c++
#define EXPAND(x) x

#define DECL_ARGS_N__(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,X,...) X
#define DECL_ARGS_N(...)\
    EXPAND(DECL_ARGS_N__(0,##__VA_ARGS__,12,11,10,9,8,7,6,5,4,3,2,1,0))
    
int len = DECL_ARGS_N(1,2);       
```
### PAIR_ARGS_N
```
#define ARG(T, P) T P
#define PARAM(T, P) P
PAIR_ARGS_N(ARG, ##__VA_ARGS__)
PAIR_ARGS_N(PARAM, ##__VA_ARGS__))
```
对于PAIR_ARGS_N的最后展开其实是一个类型,一个值.在filament中使用带类型参数列表为定义函数的参数.带PARAM的展开可以作为参数列表来使用.
```c++
#define EXPAND(x) x
```
```c++
#define APPLY0(M,...)
#define APPLY1(M, A, ...) EXPAND(M(A))
#define APPLY2(M, A, ...) EXPAND(M(A)), EXPAND(APPLY1(M, __VA_ARGS__))
#define APPLY3(M, A, ...) EXPAND(M(A)), EXPAND(APPLY2(M, __VA_ARGS__))
#define APPLY4(M, A, ...) EXPAND(M(A)), EXPAND(APPLY3(M, __VA_ARGS__))
#define APPLY5(M, A, ...) EXPAND(M(A)), EXPAND(APPLY4(M, __VA_ARGS__))
#define APPLY6(M, A, ...) EXPAND(M(A)), EXPAND(APPLY5(M, __VA_ARGS__))
#define APPLY7(M, A, ...) EXPAND(M(A)), EXPAND(APPLY6(M, __VA_ARGS__))
#define APPLY8(M, A, ...) EXPAND(M(A)), EXPAND(APPLY7(M, __VA_ARGS__))
#define APPLY9(M, A, ...) EXPAND(M(A)), EXPAND(APPLY8(M, __VA_ARGS__))
#define APPLY10(M, A, ...) EXPAND(M(A)), EXPAND(APPLY9(M, __VA_ARGS__))
#define APPLY11(M, A, ...) EXPAND(M(A)), EXPAND(APPLY10(M, __VA_ARGS__))
#define APPLY12(M, A, ...) EXPAND(M(A)), EXPAND(APPLY11(M, __VA_ARGS__))
#define APPLY_N__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, X, ...) APPLY##X
#define APPLY(M, ...) EXPAND(EXPAND(APPLY_N__(M, __VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))(M, __VA_ARGS__))


#define PAIR_ARGS_0(M, ...)
#define PAIR_ARGS_1(M, X, Y, ...) M(X, Y)
#define PAIR_ARGS_2(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_1(M, __VA_ARGS__))
#define PAIR_ARGS_3(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_2(M, __VA_ARGS__))
#define PAIR_ARGS_4(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_3(M, __VA_ARGS__))
#define PAIR_ARGS_5(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_4(M, __VA_ARGS__))
#define PAIR_ARGS_6(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_5(M, __VA_ARGS__))
#define PAIR_ARGS_7(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_6(M, __VA_ARGS__))
#define PAIR_ARGS_8(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_7(M, __VA_ARGS__))
#define PAIR_ARGS_9(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_8(M, __VA_ARGS__))
#define PAIR_ARGS_10(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_9(M, __VA_ARGS__))
#define PAIR_ARGS_11(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_10(M, __VA_ARGS__))
#define PAIR_ARGS_12(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_11(M, __VA_ARGS__))

#define PAIR_ARGS_N__(_0, E1, _1, E2, _2, E3, _3, E4, _4, E5, _5, E6, _6, E7, _7, E8, _8, E9, _9, E10, _10, E11, _11, E12, _12, X, ...) PAIR_ARGS_##X

#define PAIR_ARGS_N(M, ...) \
    EXPAND(EXPAND(PAIR_ARGS_N__(0, ##__VA_ARGS__, 12, E, 11, E, 10, E, 9, E, 8, E, 7, E, 6, E, 5, E, 4, E, 3, E, 2, E, 1, E, 0))(M, __VA_ARGS__))
```
### 例子DECL_DRIVER_API_R_N
使用了宏来定义函数
DECL_DRIVER_API_R_N展开为createRenderTarget函数的实现.
第一个参数为函数返回值,第二个参数为函数名字.参数列表通过上述宏传递.
```c++
DECL_DRIVER_API_R_N(backend::RenderTargetHandle, createRenderTarget,
        backend::TargetBufferFlags, targetBufferFlags,
        uint32_t, width,
        uint32_t, height,`
        uint8_t, samples,
        backend::MRT, color,
        backend::TargetBufferInfo, depth,
        backend::TargetBufferInfo, stencil) 
        
#define DECL_DRIVER_API_R_N(R, N, ...) \
    DECL_DRIVER_API_RETURN(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__), PAIR_ARGS_N(PARAM, ##__VA_ARGS__))
       
 #define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params)                         \
    inline RetType methodName(paramsDecl) {                                                     \
        DEBUG_COMMAND_BEGIN(methodName, false, params);                                         \
        RetType result = mDriver.methodName##S();                                               \
        using Cmd = COMMAND_TYPE(methodName##R);                                                \
        void* const p = allocateCommand(CommandBase::align(sizeof(Cmd)));                       \
        new(p) Cmd(mDispatcher.methodName##_, RetType(result), APPLY(std::move, params));       \
        DEBUG_COMMAND_END(methodName, false);                                                   \
        return result;                                                                          \
```

# CommandType命令解析

```c
template<typename T, typename Type, typename D, typename ... ARGS>
constexpr decltype(auto) invoke(Type T::* m, D&& d, ARGS&& ... args) {
    static_assert(std::is_base_of<T, std::decay_t<D>>::value,
            "member function and object not related");
    return (std::forward<D>(d).*m)(std::forward<ARGS>(args)...);
}

template<typename M, typename D, typename T, std::size_t... I>
constexpr decltype(auto) trampoline(M&& m, D&& d, T&& t, std::index_sequence<I...>) {
    return invoke(std::forward<M>(m), std::forward<D>(d), std::get<I>(std::forward<T>(t))...);
}

template<typename M, typename D, typename T>
constexpr decltype(auto) apply(M&& m, D&& d, T&& t) {
    return trampoline(std::forward<M>(m), std::forward<D>(d), std::forward<T>(t),
            std::make_index_sequence< std::tuple_size<std::remove_reference_t<T>>::value >{});
}
```
总结:  
当把可变参数作为参数传入函数,或者传入类中,使用std::tuple保存下来.
using SavedParameters = std::tuple<std::remove_reference_t<ARGS>...>;
解析: 上述函数apply->trampoline->invoke 调用函数.
  
  

