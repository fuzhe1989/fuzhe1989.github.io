---
title:      "Future与Promise"
date:       2018-01-30 11:55:30
tags:
    - 异步
    - 并发
---

Future和Promise

# 异步编程

有没有某个时刻，你觉得你的程序可以分成多个部分，其中一些部分不需要等待其它部分运行结束？比如当程序发出一个http请求后，在它返回之前，程序似乎还可以做点别的事情；比如当程序在等待一个请求的response的序列化完成时，似乎它可以做下个请求的参数检查了。这时候，你就需要了解异步编程了。

当程序分成多部分，这些部分之间的消息通信就成了一件很重要的事情。通常我们将消息通信分成同步和异步两种，其中同步就是消息的发送方要等待消息返回才能继续处理其它事情，而异步就是消息的发送方不需要等待消息返回就可以处理其它事情。很显然异步允许我们同时做更多事情，往往也能获得更高的性能。尤其对于JavaScript这种通常是单线程环境的语言，更需要将长延时的阻塞操作异步化来保证其它操作的顺利进行。

异步编程的核心问题是如何处理通信：要么有办法知道通信有没有完成，要么能保证在通信完成后执行一段特定的逻辑。前者就是通知机制，比如信号量、条件变量等；后者就是callback，即回调。

## 回调噩梦

当一项任务需要分成多个异步阶段完成时，就需要在每个阶段的回调函数中加入下阶段回调的代码，最终产生下面这样金字塔形状的代码：

```js
getData = function(param, callback){
  $.get('http://example.com/get/'+param,
    function(responseText){
      callback(responseText);
    });
}

getData(0, function(a){
  getData(a, function(b){
    getData(b, function(c){
      getData(c, function(d){
        getData(d, function(e){
         // ...
        });
      });
    });
  });
});
```

可以想象当回调层次继续增加时，代码有多恐怖。这就是回调噩梦。

# Future和Promise

Future指一个**只读**的值的容器，这个值可能立即可用，也可能在未来某个时间可用。而Promise则是一个只能写入一次的对象。每个Promise关联一个Future，对Promise的写入会令Future的值可用。我们只讨论Promise和Future一对一的场景，在这个场景中Future就是值，而Promise是产生值的方法。

Future和Promise来源于函数式语言，其目的是分离一个值和产生值的方法，从而简化异步代码的处理。

## 通知机制

Future与Promise配合起来可以实现一种可靠的通知机制，即我们可以异步执行一个方法，通过返回的Future来知道异步方法何时结束、是否成功、返回值是什么。

```cpp
// 调用方
void SyncOperation() {
    Promise<int> promise;
    RunAsync(std::bind(AsyncFunc, promise));
    Future<int> future = promise.GetFuture();
    int result = future.Get(); // wait until future is done
}
// 接收方
void AsyncFunc(Promise<int> promise) {
    // do something
    promise.Done(result);
}
```

## 链式回调

Promise的一个重要特性就是它支持`then`，可以将金字塔式的回调组织为链式，极大地降低了理解和维护的难度：

```js
getData = function(param, callback){
  return new Promise(function(resolve, reject) {
    $.get('http://example.com/get/'+param,
    function(responseText){
      resolve(responseText);
    });
  });
}

getData(0).then(getData)
  .then(getData)
  .then(getData)
  .then(getData);
```

## Async和Await

C#在5.0之后支持了`async`和`await`关键字，允许写出这样的代码：

```csharp
async Task<int> AccessTheWebAsync()  
{   
    HttpClient client = new HttpClient();  
    Task<string> getStringTask = client.GetStringAsync("http://msdn.microsoft.com");  
    DoIndependentWork();  
    string urlContents = await getStringTask;  
    return urlContents.Length;  
}

string urlContents = await client.GetStringAsync();
```

其中`async`要求函数必须返回`Task`或`Task<T>`，这里的`Task`可以理解为一种Future。用`async`修饰函数表明这是个可异步执行的函数，而用`await`会等待Future结束，返回Future的值，将异步又转成了同步。

上面js的例子用`await`来实现就是：

```js
getData = async function(param, callback){
  return new Promise(function(resolve, reject) {
    $.get('http://example.com/get/'+param,
    function(responseText){
      resolve(responseText);
    });
  });
}

var data = await getData(0);
var data1 = await getData(data);
var data2 = await getData(data1);
var data3 = await getData(data2);
var data4 = await getData(data3);
```

这种写法要比Promise链更接近同步，也更易懂，但其底层依然是Promise。这种写法很接近于协程：用Promise来实现yield和resume，它就是一种协程。

## 不同语言中的Future和Promise

### C++

C++11中增加了`std::future`和`std::promise`，基本是按照Future只读、Promise只写来设计的。它的缺点是：

1. 其实现绑定了`std::thread`，很难扩展到其它执行器上。
2. Promise不支持链式回调。

### C#

C#的`Task`就类似于Future，它的`async`和`await`也很方便。

### Java

Java之前就有`Future`，类似于C++11的`std::promise`，没有链式回调能力。Java8中增加了`CompletableFuture`，可以认为是一个完全的Promise了。

### JavaScript

ES6开始，JavaScript增加了Promise、async、await等特性，极大改善了JS代码中维护回调难的问题。

### Python

Python3.5之后增加了对`async`和`await`的支持。

### Scala

Scala中的`Future`与`Promise`完全符合上面的介绍，它的一个特点是一个`Future`可以增加多个回调，但不保证这些回调的执行顺序。

# 相关链接

* [Futures and promises](https://en.wikipedia.org/wiki/Futures_and_promises)
* [Futures and Promises](http://dist-prog-book.com/chapter/2/futures.html)
* [并发编程 Promise, Future 和 Callback](http://ifeve.com/promise-future-callback/)
* [理解 Promise 的工作原理](https://blog.coding.net/blog/how-do-promises-work)
* [Scala: FUTURES AND PROMISES](https://docs.scala-lang.org/overviews/core/futures.html)
* [Understand promises before you start using async/await](https://medium.com/@bluepnume/learn-about-promises-before-you-start-using-async-await-eb148164a9c8)
* [Await](https://en.wikipedia.org/wiki/Await)
* [Asynchronous programming with async and await (C#)](https://docs.microsoft.com/en-us/dotnet/csharp/programming-guide/concepts/async/index)
