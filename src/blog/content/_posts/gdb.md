title: gdb
author: xuezc
tags:
  - linux
  - gdb
categories:
  - linux
abbrlink: 10f4aa60
description: gdb 的使用
date: 2023-03-31 11:02:00
---
printf  printk
GDB
命令行调试器
gdb gdbserver
查看变量的类型：whatis 变量名
看函数的堆栈：bt
n 10 :next 10行
要启动反向调试,要先开启记录:record
停止 record stop
查看堆栈 bt
查看线程 info thread
切换线程 thread *
/////////////////打印sockaddr/////////////////////
p *((struct sockaddr_in_or_whichever_you_use *) pointer_to_struct_sockaddr)
p *((struct sockaddr_in*) pointer_to_struct_sockaddr)
struct sockaddr from;
 /*
     ...
        working 
     ...
*/
 struct sockaddr_in *sock = ( struct sockaddr_in*)&from;
 int port = ntohs(sock->sin_port);
sdshdr8
((struct sdshdr16 *)((c->querybuf)-(sizeof(struct sdshdr16))))


(2)frame或f：查看栈上某一层的消息(包括打印当前栈层消息)。
*frame <n>：n是一个从0开始的整数，是栈中的层编号，不打n，显示当前栈层。
*up <n>：表示向栈上面移动n层的，可以不打n，表示向上移动一层。
*down <n>：表示向栈下面移动n层的，可以不打n，表示向下移动一层。
*info frame：打印更为详细的当前栈层的消息。

jump用法,跳到指定的函数,中间不执行,并且jump到指定函数会继续执行,所有一般是先b后j
***多线程调试***------>set scheduler-locking on 设置当前线程开关 On---只调试当前线程

在 GDB 中使用 signal 函数手动给程序发送信号，这里就是 signal SIGINT；
改变 GDB 信号处理的设置，通过 handle SIGINT nostop print 告诉 GDB 在接收到 SIGINT 时不要停止，并把该信号传递给调试目标程序 。

tb 临时断点,到达自动删除;
u === until结束当前循环
set print pretty on	设置输出结构体

设置字符长度 set print elements 0 

set print pretty off
关闭printf pretty这个选项，GDB显示结构体时会如下显示：
$1 = {next = 0x0, flags = {sweet = 1, sour = 1}, meat = 0x54"Pork"}
show print pretty
查看GDB是如何显示结构体的。
set print sevenbit-strings
设置字符显示，是否按“\nnn”的格式显示，如果打开，则字符串或字符数据按\nnn显示，如“\065”。
show print sevenbit-strings
查看字符显示开关是否打开。
配置好默认gdb文件启动 gdb -x filename
A->B->C
A->C
变量值
寄存器
内存
stack

/////////////////查看参数//////////////////////
info args


GDB
1.run
2.设置break 可以设置条件断点
3.任何时候停止，并查看当前进程所有的一切
4.动态改变执行环境

命令：
	list  l  查看代码
	break b  设置断点  b 行号/函数名
	info  b  查看断点
	run	  r  运行程序到第一个断点
	next  n  运行下一行 不进入函数
	step  s 进入函数
	continue c 跳到下一断点
	print  p  打印某个变量
	bt    函数调用栈
	finish 结束当前函数
	quit  q  退出gdb
	until 或 u 退出循环体
	gdb filename
	gdb filename pid
	examine  x
	gdb>set args 10 20 30 40 50
	  
	如何进行反编译
	disassemble
	disassemble /m _ZN5pointC1Eii（readelf  c++filter看出来的符号表）
	shell +命令
	
	 暂停/恢复程序的方法
	1 断点
		break   b 
		break function
		break offset
		break filename:function /offset
		查看设置的断点  info b
		break *address
		条件断点
		break ...if<condition>
		 int a ,b ,c;
		 funa(a);
		 funb(b);
		 func(c);
		 break func if<a==b>
		 eg: break 20 if i==89
		clear number 删除某行所有的断点
		del 断点号
	 2 观察点
		watch  当变量被写入（变化）的时候 就停止程序
		rwatch 当该变量被读取的时候 就停止程序
		awatch  当该变量被访问时 就停止程序
		info watchpoint
		
	 捕捉点
	 信号
	 线程暂停
	 
	 3.捕捉点
		catch 
		1.catch throw
		2.catch exec  捕捉系统发生execve系统调用时 暂停程序
		3.catch fork 
		4.catch vfork
		5.catch load 
		6.catch unload
	
	4.
	查看堆栈 bt
	
GDB的特殊用法
watch 
monitor 
print 
catch
//--------------------------------------------------------------------//
	如果一个作业已经在前台执行，可以通过ctrl+z将该作业放到后台并挂起。然后通过jobs命令查看在后台执行的作业并找到对应的作业ID，执行bg %n(n为通过jobs查到的作业ID)唤醒该作业继续执行。
	该方式也存在结果会输出到终端上的情况，同样可以用重定向的方法解决
	相关命令：
	jobs------------查看在后台执行的进程
	fg %n----------将后台执行进程n调到前台执行，n表示jobnumber（通过jobs查看的进程编号，而非pid）
	ctrl+z----------将在前台执行的进程，放到后台并挂起
	bg %n---------将在后台挂起的进程，继续执行




gdb test-1
gdb attach pid
gbd test-1 core



l main 查看main上下5行代码
l 10	10行
set listsize 20 上下20行
l test-1.c:10 //l test-1.c:main
视图：
focus
layout

调试fork中的子进程可以这样设置:(gdb)set follow-fork-mode child
set args 在gdb启动后可以这样设置

//-------------------------------------------------------------//
当程序被停住了，首先要确认的就是程序是在哪儿被断住的。这个一般是通过查看调用栈信息来看的。在gdb中，查看调用栈的命令是backtrace，可以简写为bt。
也可以通过info stack命令实现类似的功能（我更喜欢这个命令）：

1. 通过finish命令运行至函数结束，此时会打印函数返回值。
    (gdb) finish
    Run till exit from #0 foo () at main.c:9
    main () at main.c:15
    15 }
    Value returned is $2 = 100

2. 返回值会存储在eax寄存器中，通过查看信息可以获取返回值。
    (gdb) p $eax
    $3 = 100

    (gdb) info registers 
    eax 0x64 100
//-------------------------------------------------------------//	
quit
加断点：break b 20
查看断点	info breakpoint
disable breakpoint 断点号 enable breakpoint 断点号
清除断点 delete 断点号
执行	run r
步入	step s
二进制步入	stepi si	显示display/i $pc
步出	next n
跳出	finish f
until 或 u 退出循环体
continue c 跳到下一断点
bt    函数调用栈
finish 结束当前函数
查看变量值：info local 变量名
查看寄存器：i r	//info registers eax
在调试过程中给变量赋值：set args 可指定运行时参数
shell:linux命令
print  p  打印某个变量	print /f #f表示格式,如 print /x 按十六进制显示
查看某个地址的内存
x 格式  地址
/8ub
8:显示8个
u:无符号十进制显示
b:byte  
h：half word 
w：word

如何查看数组的内容
int a[10];
char a[10]
p *array@len  
p *a@10 


flag
0 100 
条件watch
0  1 100 condition 
能够令程序暂停和恢复运行的命令：
暂停：
	Breakpoint 
	watchpoint
	catchpoint 
	signals
	thread stop

breakpoint
  break func 
  b   num 
  b filename:linenum
  b filename:funname
  b   *address  内存断点 
  break....if<condition>  条件断点
  break  if i==50  
  info breakpoints
  
watchpoint
  watch expr  
  rwatch expr 		read
  awatch expr 		
  info   watchpoints

catchpoint
  catch event
	catch  c++捕捉到的异常
	throw  c++抛出的异常
	exec   Linux调用系统调用exec 
	fork   Linux调用系统调用fork  创建进程
	vfork  调用系统调用vfork 创建线程
	load 或者 load libname  载入动态链接库
	 ex: load pthread    pthread_creat 
	unload 或者 unload libname  卸载dll  .so 
	
	tcatch event 一次性的捕捉点
	
	info catchpoints
		
clear 
delete 
disable 
enable

自动化脚本设计

commands breaknum
.....command list ...
end
commands
printf "x is %d\n",x
continue 
end

signals
	ctl+c  ----》SIGINT
	kill pid   kill -9 pid 
	-9  SIGKILL
	SIGCHLD   SIGSEMG 段错误
	
	handle <signal> <keywords>
	keywords 取值
	nostop  接收到指定信号后不会停止程序，但会带引出消息通知你收到了这个信号
	stop    
	print  会打印通知
	noprint 
	noignore 当调试程序收到指定信号后，gdb不会处理这个信号 ，交由调试程序处理
	ignore 当调试程序收到指定信号后，调试程序不会处理  

	handle SIGINT ignore 
	
	info signals
	info handle 
	
thread stops 
	break filename:<linespec> thread <threadno>
	break filename:<linespec> thread <threadno>   if <condition>
	
	threadno  线程ID 是由GDB来分配
	info threads 查看当前线程ID
	如果不指定那么就将该断点设置在所有的线程上
	
	b test-1.c:20 thread 10 if x>100 
	在某个线程被断的时候，GDB会停止所有的线程 
	
display 
	display result 
	display/<fmt> <value>
	display/i $pc
	undisplay  <displaynum>
	info display
	disable  display   <displaynum>
	enable display  <displaynum>
	delete  display  <displaynum>
	
print 
	set print pretty on
	$1={
	  a =100,
	   b=20,
	   c=90
	}
	set print pretty off 
	$1={a =100, b=20,  c=90}
	如何改变变量的值
	
	print i=99
	
jump: 改变程序运行轨迹的方法	
	jump <linenum> 
	jump <address>
	set $pc =address
	
	call <func>




//------------------------------------------------------//
printf  printk
GDB
命令行调试器
gdb gdbserver

A->B->C
A->C
变量值
寄存器
内存
stack

-g
gdb target


GDB
1.run
2.设置break 可以设置条件断点
3.任何时候停止，并查看当前进程所有的一切
4.动态改变执行环境

命令：
	list  l  查看代码
	break b  设置断点  b 行号/函数名
	info  b  查看断点
	run	  r  运行程序到第一个断点
	next  n  运行下一行 不进入函数
	continue c 跳到下一断点
	print  p  打印某个变量
	bt    函数调用栈
	finish 结束当前函数
	quit  q  退出gdb
	until 或 u 退出循环体
	gdb filename
	gdb filename pid
	examine  x
	gdb>set args 10 20 30 40 50
	  
	如何进行反编译
	disassemble
	disassemble /m _ZN5pointC1Eii（readelf  c++filter看出来的符号表）
	shell +命令
	
	 暂停/恢复程序的方法
	1 断点
		break   b 
		break function
		break offset
		break filename:function /offset
		查看设置的断点  info b
		break *address
		条件断点
		break ...if<condition>
		 int a ,b ,c;
		 funa(a);
		 funb(b);
		 func(c);
		 break func if<a==b>
		 eg: break 20 if i==89
		 
	 2 观察点
		watch  当变量被写入（变化）的时候 就停止程序
		rwatch 当该变量被读取的时候 就停止程序
		awatch  当该变量被访问时 就停止程序
		info watchpoint
		
	 捕捉点
	 信号
	 线程暂停
	 
	 3.捕捉点
		catch 
		1.catch throw
		2.catch exec  捕捉系统发生execve系统调用时 暂停程序
		3.catch fork 
		4.catch vfork
		5.catch load 
		6.catch unload
	
gbd调试signal时：
(gdb)signal SIGINT	继续


