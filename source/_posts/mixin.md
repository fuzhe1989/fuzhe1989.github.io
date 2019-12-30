---
title:      "通过mixin组合功能"
date:       2018-04-21 19:41:43
tags:
    - C++
    - 编程
---

注：本文非原创，内容来自若干相关文章和回答。

# [Wikipedia - Mixin](https://en.wikipedia.org/wiki/Mixin)

在OOP中，mixin是一种特殊的类，它的方法用于给其它类使用，但它在逻辑上并不是这些类的父类。相比于“被继承”，它更接近于“被包含”。

Mixin鼓励代码重用，且不会有多重继承导致的歧义（菱形问题）。mixin也可以看作是方法有实现的接口，是[“依赖反转原则”](https://en.wikipedia.org/wiki/Dependency_inversion_principle)的一个例子。

mixin可以通过继承来实现。包含功能的mixin类作为父类，而需要它功能的类就继承自它，但这不是接口与实现的关系：子类依然可以继承父类的特性，但不必“is a”父类。

优点：
* 提供了一种多重继承的机制，允许多个类使用相同的功能，而不会有多重继承的复杂机制。
* 代码重用性：想在不同类之间共享功能时，mixin很有用。
* mixin允许只从父类继承一部分功能，而不是全部功能。

js的例子：
```js
const Halfling = function (fName, lName) {
    this.firstName = fName;
    this.lastName = lName;
};

const mixin = {
    fullName() {
        return this.firstName + ' ' + this.lastName;
    },
    rename(first, last) {
        this.firstName = first;
        this.lastName = last;
        return this;
    }
};

// An extend function
const extend = (obj, mixin) => {
    Object.keys(mixin).forEach(key => obj[key] = mixin[key]);
    return obj;
};

const sam = new Halfling('Sam', 'Loawry');
const frodo = new Halfling('Freeda', 'Baggs');

// Mixin the other methods
extend(Halfling.prototype, mixin);

console.log(sam.fullName());  // Sam Loawry
console.log(frodo.fullName());  // Freeda Baggs

sam.rename('Samwise', 'Gamgee');
frodo.rename('Frodo', 'Baggins');

console.log(sam.fullName());  // Samwise Gamgee
console.log(frodo.fullName());  // Frodo Baggins
```

# [Stackoverflow - Mixin vs inheritance](https://stackoverflow.com/questions/860245/mixin-vs-inheritance)

mixin通常伴随着多重继承，从这个角度讲它和继承没有区别。

区别在于，mixin很少作为独立对象。泛泛而谈，mixin就是继承，但它的角色与接口继承是不一样的，它是一个基本类或组件，是用来组合的。“mixin”这个名字就暗示着它是用来与其它代码混入的，也意味着mixin类不能独立使用。

mixin类应该是平铺的，线性的，而不应该是树形的。

mixin是（多重）继承的受限的特例，用于继承实现，Ruby等语言允许mixin，但不允许通用概念的多重继承。

mixin与抽象基类（接口）的对比：两者都是不希望被实例化的父类。mixin是提供功能而不直接使用，用户使用子类。抽象基类（接口）是提供接口而不提供功能，用户使用接口。

# C++的mixin

链接：
* [Stackoverflow - What are Mixins (as a concept)](https://stackoverflow.com/questions/18773367/what-are-mixins-as-a-concept)
* [Stackoverflow - What is C++ Mixin-Style?](https://stackoverflow.com/questions/7085265/what-is-c-mixin-style)

mixin是来自lisp的一个概念，一个解释是：

> 一个mixin就是类里的一小块，它就是用来与其它类或mixin做组合的。
> ...
> 一个独立的类（如Person）与一个mixin的区别在于，一个mixin只建模小的功能点（如printing或displaying），不是用来独立使用的，而是给其它需要这个功能的类做组合的。

mixin要解的问题：如何建模一系列正交概念。可以用继承来做，每个概念变成一个接口，然后具体类去实现这些接口。

继承的问题是无法自由的组合这些具体类。

mixin是给一组基础类，每个都建模了一个抽象概念，且能直接组合成一个新的类，像乐高一样，满足需求。如果你新增了基础类，只要它是和其它基础类正交的，就可以扩展到这个组合类里。

C++中的一种技术是结合模板和继承，通过模板参数来组合基础类。一个例子：

```cpp
struct Number {
    typedef int value_type;
    int n;
    void set(int v) { n = v; }
    int get() const { return n; }
};

template <typename BASE, typename T = typename BASE::value_type>
struct Undoable : public BASE {
    typedef T value_type;
    T before;
    void set(T v) { before = BASE::get(); BASE::set(v); }
    void undo() { BASE::set(before); }
};

template <typename BASE, typename T = typename BASE::value_type>
struct Redoable : public BASE {
    typedef T value_type;
    T after;
    void set(T v) { after = v; BASE::set(v); }
    void redo() { BASE::set(after); }
};

typedef Redoable< Undoable<Number> > ReUndoableNumber;

int main() {
  ReUndoableNumber mynum;
  mynum.set(42); mynum.set(84);
  cout << mynum.get() << '\n';  // 84
  mynum.undo();
  cout << mynum.get() << '\n';  // 42
  mynum.redo();
  cout << mynum.get() << '\n';  // back to 84
}
```

（另一种方法是通过CRTP来实现，参见[这里](/2018/04/21/crtp/)）

# [MIXINS AS ALTERNATIVE TO INHERITANCE IN JAVA 8](http://hannesdorfmann.com/android/java-mixins)

假如有两个类`Ship`和`Airport`：

```java
public class Ship {
    List<Cargo> cargoes;

    public void addCargo(Cargo c) {
        cargoes.add(c);
    }

    public void removeCargo(Cargo c) {
        cargoes.remove(c);
    }
}

public class Airport {
    List<Aircraft> aircrafts;

    public void land(Aircraft a) {
        aircrafts.add(a);
    }

    public void depart(Aircraft a) {
        aircrafts.remove(a);
    }
}
```

假如我们要写一个新类，既是`Ship`又是`Airport`，怎么写？多继承是不可能的了：

```java
class AircraftCarrier extends Airport, Ship // doesn't compile
```

把`Ship`和`Airport`改成接口，再写个实现类？

```java
public interface Ship {
    void addCargo(Cargo c);
    void removeCargo(Cargo c);
}

public interface Airport {
    void land(Aircraft a);
    void depart(Aircraft a);
}

class AircraftCarrier implements Ship, Airport {
    List<Aircraft> aircrafts;
    List<Cargo> cargoes;

    public void land(Aircraft a) {
        aircrafts.add(a);
    }

    public void depart(Aircraft a) {
        aircrafts.remove(a);
    }

    public void addCargo(Cargo c){
        cargoes.add(c);
    }

    public void removeCargo(Cargo c){
        cargoes.remove(c);
    }
}
```

太繁琐，没有能重用代码，有时候还没办法改。

想想Ruby里怎么做？

```ruby
module Airport  # for simplicity: module is same as class
    @aircrafts = Array.new    # variable

    def land(aircraft)
        aircrafts.push(aircraft)
    end

    def depart(aircraft)
        aircraft.delete(aircraft)
    end
end


class AircraftCarrier
    include Ship
    include Airport
end
```

这里我们只要把`Ship`和`Airport`结合起来，不需要通过继承。注意这不是多重继承场景。Ruby原生支持mixin。Scala里有类似的特性叫Traits：

```scala
trait Ship {
    val cargoes : ListBuffer[Cargo]

    def addCargo(c : Cargo){
        cargoes += c
    }

    def removeCargo(c : Cargo){
        cargoes -= c
    }
}

class AircraftCarrier with Ship with Airport
```

Java8里有了default method后，我们也可以做得漂亮：

```java
public interface Airport {
    // To be implemented in subclass
    List<Aircraft> getAircrafts();

    default void land(Aircraft aircraft) {
        getAircrafts().add(aircraft);
    }

    default void depart(Aircraft aircraft) {
        getAircrafts.remove(aircraft);
    }
}

class AircraftCarrier implements Ship, Airport {
    List<Aircraft> aircrafts = new ArrayList<>();
    List<Cargo> cargoes = new ArrayList<>();

    @Override
    public List<Aircraft> getAircrafts(){
        return aircrafts;
    }

    @Override
    public List<Cargo> getCargoes(){
        return cargoes;
    }
}
```

还能继续扩展：

```java
class Houseboat implements House, Ship { ... }
class MilitaryHouseboat implements House, Ship, Airport { ... }
```

kotlin也用default method来提供mixin功能：

```kotlin
interface Ship {
    val cargoes : List<Cargo>

    fun addCargo(c : Cargo){
        cargoes.add(c)
    }

    fun removeCargo(c : Cargo) {
        cargoes.remove(c)
    }
}

class AircraftCarrier : Ship, Airport {
    override val cargoes = ArrayList()
    override val aircrafts = ArrayList()
}
```

希望你也认同：mixin是比传统的inheritance更好的选择。

建议：

> inheritance扩展性不好！用inheritance只能继承性质，而不能继承功能！mixin更适合于继承功能。

# [Go的mixin](https://stackoverflow.com/questions/40091142/is-there-an-established-pattern-name-for-golang-code-which-seems-similar-to-a-mi)

go中我们一般通过组合来扩展功能。想象我们有一个接口`Yeller`：

```go
type Yeller interface {
    Yell(message string)
}

func Yell(m Yeller, message string) {
    m.Yell(message)
}
```

和一个实现了这个接口的类型`Person`：

```go
type Person struct {}

func (p *Person) Yell(message string) { /* yell */ }

// Let a person yell.
person := &Person{}
Yell(person, "No")
```

但假如我们不想让`Person`实现`Yell`，就需要通过组合的方式来做：

```go
type Person struct {
    Yeller Yeller
}

person := &Person{ /* Add some yeller to the mix here */ }
```

但此时`Person`本身还是没办法作为一个`Yeller`来使用，因为它没有实现`Yell`：

```go
// Won't work
Yell(person, "Loud")
```

相反，我们要显式通过`Person.Yeller`来调用`Yell`：

```go
// Will work
Yell(person.Yeller, "No")
```

否则就还是要为`Person`实现`Yell`接口。

但我们可以用mixin来扩展`Person`，方法就是只写成员类型，而不写成员名字：

```go
type Person struct {
    Yeller
}

p := &Person { /* Add some Yeller to the mix here */ }
Yell(p, "Hooray")
```

当我们把`*Person`作为`Yeller`使用时，go会先检查`*Person`支不支持`Yell`接口，如果不支持，再检查`Person`中是否有实现了`Yell`接口的没有名字的成员，有的话，就会认为`*Person`满足`Yeller`接口的要求，实际上调用了那个成员的方法。

这里`Person.Yeller`就是一个mixin。

而且`Person`还可以包含更多mixin，从而支持更多接口：

```go
type Yeller interface {
    Yell(message string)
}

func Yell(y Yeller, message string) {
    y.Yell(message)
}

type Whisperer interface {
    Whisper(message string)
}

func Whisper(w Whisperer, message string) {
    w.Whisper(message)
}

type Person struct {
    Yeller
    Whisperer
}
```

我们通过为`Person`混入两个mixin类型，令`Person`自动获得了两个接口的功能，且不需要写代码来实现功能的代理。这就是mixin的价值。

# [C++中通过mixin来重用功能](http://www.thinkbottomup.com.au/site/blog/C%20%20_Mixins_-_Reuse_through_inheritance_is_good)

传统观念告诉我们通过继承来重用代码是邪恶的。大多数文章会鼓励用组合和代理代替继承来做代码重用。但组合与代理也会有它自己的问题，所以更好的方法是什么呢？

本文我会介绍C++中的mixin概念。它会告诉你在C++中通过mixin来重用代码并不邪恶，事实上它的诸多优点应该传播给每个C++程序员。

C++的mixin的教程多少会有些令人困惑，还会用一些生造的例子来让你纳闷为什么自己要学这么晦涩的技术。本文的目的是以易于理解的方式介绍mixin，它的实现，以及对比其它代码重用的方案。

## 例子

假设有个Task管理框架，可以做异步处理，Task接口为：

```cpp
struct ITask {
    virtual ~ITask() {}
    virtual std::string GetName() = 0;
    virtual void Execute() = 0;
};
```

我们的需求是有些通用功能应该能重用。这里用timing和logging来示例。

## 方法1：通过继承重用（邪恶）

```cpp
// abstract base class which implements Execute() and provides logging feature. Derived classes must implement OnExecute().
class BaseLoggingTask : public ITask {
public:
    void Execute() override {
        std::cout << "LOG: The task is starting - " << GetName() << std::endl;
        OnExecute();
        std::cout << "LOG: The task has completed - " << GetName() << std::endl;
    }
 
    virtual void OnExecute() = 0;
};
 
// Concrete Task implementation that reuses the logging code of the BaseLoggingTask
class MyTask : public BaseLoggingTask {
public:
    void OnExecute() override {
        std::cout << "...This is where the task is executed..." << std::endl;
    }
 
    std::string GetName() override {
        return "My task name";
    }
};
```

上面的代码看起来挺合理的，该重用的代码都推到基类里了，代码很简洁很好懂。接下来处理timing：

```cpp
// abstract base class which implements Execute() and provides task timing feature.  Derived classes must implement OnExecute().
class BaseTimingTask : public ITask {
    Timer mTimer;
public:
    void Execute() override {
        mTimer.Reset();
        OnExecute();
        double t = mTimer.GetElapsedTimeSecs();
        std::cout << "Task Duration: " << t << " seconds" << std::endl;
    }
 
    virtual void OnExecute() = 0;
};
 
// Concrete Task implementation that reuses the timing code of the BaseTimingTask
class MyTask : public BaseTimingTask {
public:
    void OnExecute() override {
        std::cout << "...This is where the task is executed..." << std::endl;
    }
 
    std::string GetName() override {
        return "My task name";
    }
};
```

但如果我们要同时重用logging和timing的代码该怎么办？两个`Execute`冲突了！这显示了这种“将重用部分放到基类”方法的主要局限性。某个时刻你会发现没办法把想重用的部分组合起来。

另一个问题是这里用了虚函数，而且是在虚函数内调用虚函数，这样编译器没办法inline，会增加开销。

## 方法2：通过组合与代理重用代码（还是邪恶，没那么严重）

```cpp
class LoggingTask : public ITask {
    std::unique_ptr<ITask> mTask;
public:
    explicit LoggingTask(ITask* task) : mTask(task) {}
 
    void Execute() override {
        std::cout << "LOG: The task is starting - " << GetName() << std::endl;
        if (mTask) mTask->Execute();
        std::cout << "LOG: The task has completed - " << GetName() << std::endl;
    }
 
    std::string GetName() override {
        if (mTask) {
            return mTask->GetName();
        } else {
            return "Unbound LoggingTask";
        }
    }
};
```

概念上很简单，LoggingTask会转发请求给它持有的真正做事情的ITask。

```cpp
class TimingTask : public ITask {
    std::unique_ptr<ITask> mTask;
    Timer mTimer;
public:
    explicit TimingTask(ITask* task) : mTask(task) {}
 
    void Execute() override {
        mTimer.Reset();
        if (mTask) mTask->Execute();
        double t = mTimer.GetElapsedTimeSecs();
        std::cout << "Task Duration: " << t << " seconds" << std::endl;
    }
 
    std::string GetName() override {
        if (mTask) {
            return mTask->GetName();
        } else {
            return "Unbound TimingTask";
        }
    }
};
```

上面两个类型也可以组合起来：

```cpp
// MyTask is written with no coupling to the reusable logging and timing task
class MyTask : public ITask {
public:
    void Execute() override {
        std::cout << "...This is where the task is executed..." << std::endl;
    }
 
    std::string GetName() override {
        return "My task name";
    }
};
 
// Add timing and logging to MyTask through composition and delegation
std::unique_ptr<Itask> t(new LoggingTask(new TimingTask(new MyTask())));
t->Execute();
```

问题依旧，需要在堆上分配、需要判断null、有虚函数开销。

但它解决了两个功能无法组合在一个类里的问题。每块的解耦也不错。另一个优点是每个类都直接继承自ITask，不需要再搞出一层虚函数。

但我觉得它还有以下缺陷：
* 有不必要的开销：堆分配、null检查、虚函数。
* 像LoggingTask这样的类不想提供GetName这样的接口，但你继承自ITask就要实现这个接口。假如ITask还有其它接口，LoggingTask都必须实现。

因此我认为这种方法仍然是邪恶的。

## 方法3：重返继承（Clayton重用）

我们把“将重用部分放到基类”换成“将重用部分放到派生类”。我称它为[Clayton重用](http://en.wikipedia.org/wiki/Claytons)。下面的例子看起来有点生硬，但它是通往mixin的铺路石。

```cpp
class MyTask : public ITask {
public:
    void Execute() override {
        std::cout << "...This is where the task is executed..." << std::endl;
    }
 
    std::string GetName() override {
        return "My task name";
    }
};

class TimingTask : public MyTask {
protected:
    void Execute() override {
        std::cout << "(start timer)" << std::endl;
        MyTask::Execute();
        std::cout << "(end timer)" << std::endl;
    }
};

class LoggingTask : public TimingTask {
protected:
    void Execute() override {
        std::cout << "LOG: The task is starting: " << GetName() << std::endl;
        TimingTask::Execute();
        std::cout << "LOG: The task has completed: " << GetName() << std::endl;
    }
};
```

我们得到了什么？logging的代码与timing的代码与MyTask的代码耦合在了一起，完全没办法拿出来给其它类型重用。看起来全是缺点。

但除了代码耦合与缺乏重用，我们克服了以下问题：
* 不需要堆分配。
* 没有运行期检查null。
* 不需要显式管理生命期。
* 没有多余的虚函数定义及其调用。
* 编译器有机会做更多优化。

因此，我们要找到方法解耦这些类，允许其它类重用它们。

## 方法4：通过mixin重用代码

mixin本身是一个很宽泛的概念，在C++中一般是通过参数化基类来实现：

```cpp
template <typename T>
class MyMixin : public T {};
```

这就是解决上面方法3问题的关键。

```cpp
template <typename T>
class LoggingTask : public T {
public:
    void Execute() {
        std::cout << "LOG: The task is starting - " << GetName() << std::endl;
        T::Execute();
        std::cout << "LOG: The task has completed - " << GetName() << std::endl;
    }
};

template <typename T>
class TimingTask : public T {
    Timer mTimer;
public:
    void Execute() {
        mTimer.Reset();
        T::Execute();
        double t = mTimer.GetElapsedTimeSecs();
        std::cout << "Task Duration: " << t << " seconds" << std::endl;
    }
};
 
class MyTask {
public:
    void Execute() {
        std::cout << "...This is where the task is executed..." << std::endl;
    }
 
    std::string GetName() {
        return "My task name";
    }
};
```

这里LoggintTask和TimingTask就是mixin，而MyTask则是想要重用这两个功能的具体类型。

通过mixin来构建一个对象太简单了：

```cpp
// A plain old MyTask
MyTask t1;
t1.execute();
 
// A MyTask with added timing:
TimingTask<MyTask> t2;
t2.Execute();
 
// A MyTask with added logging and timing:
LoggingTask<TimingTask<MyTask>> t3;
t3.Execute();
 
// A MyTask with added logging and timing written to look a bit like the composition example
typedef LoggingTask<TimingTask<MyTask>> Task;
Task t4;
t4.Execute();
```

但这样一个类没办法用于我们的Task管理框架，因为它没实现ITask接口，这就是最后一个mixin的功能：

```cpp
template <typename T>
class TaskAdapter : public ITask, public T {
public:
    void Execute() override {
        T::Execute();
    }
 
    std::string GetName() override {
        return T::GetName();
    }
};

// typedef for our final class, inlcuding the TaskAdaTpter<> mixin
typedef TaskAdapter<LoggingTask<TimingTask<MyTask>>> ask;
 
// instance of our Task - note that we are not forced into any heap allocations!
Task t;
 
// implicit conversion to ITask* thanks to the TaskAdapter<>
ITask* it = &t;
it->Execute();
```

# [C++中解决mixin的构造问题](http://www.drdobbs.com/cpp/mixin-based-programming-in-c/184404445)

mixin是用于与其它类或mixin组合的碎片类。它与一个正常的，独立的类的区别在于，mixin只建模一个小块功能，且不倾向于独立使用。相反，它是用于组合到那些需要这个功能的类中。oop中的一种mixin模式包含了类与多重继承。这个模型下一个mixin就是一个类，我们通过多重继承来将多个mixin类组合起来。另一种模型是用参数化继承。这种场景下我们将一个mixin类表示为派生自它的模板参数的模板类：

```cpp
template <typename Base> 
class Printing : public Base {...};
```

有些人称mixin类为“抽象子类”，即没有具体的基类的子类。C++里这种参数化继承的mixin已经用于实现高度可配置化的、协作式的、分层设计。

本文会展示一种解决C++中基于参数化继承的mixin会遇到的构造问题的解法。

```cpp
class Customer {
public:
    Customer(const char* fn, const char* ln)
        : mFirstName(fn), mLastName(ln) {}
    void Print() const {
        cout << mFirstName
        << ' '
        << mLastName;
    }
private:
    const char* mFirstName,
    const char* mLastName;
};
 
template <typename Base>
class PhoneContact: public Base {
public:
    PhoneContact(const char* fn, const char* ln, const char* pn)
        : Base(fn,ln), mPhone(pn) {}
    void Print()() const {
        Base::Print();
        cout << ' ' << mPhone;
    }
private:
    const char* mPhone;
};
 
template <typename Base>
class EmailContact: public Base {
public:
    EmailContact(const char* fn, const char* ln, const char* e)
        : Base(fn,ln),mEmail(e) {}
    void Print() const {
        Base::Print();
        cout << ' ' << mEmail;
    }
private:
    const char* mEmail;
};
 
int main() {
    Customer c1("Teddy","Bear");
    c1.Print(); cout << endl;
    PhoneContact<Customer> c2("Rick","Racoon","050-998877");
    c2.Print(); cout << endl;
    EmailContact<Customer> c3("Dick","Deer","dick@deer.com");
    c3.Print(); cout << endl;
    // The following composition isn't legal because there
    // is no constructor that takes all four arguments.
    // EmailContact<PhoneContact<Customer> >
    // c4("Eddy","Eagle","049-554433","eddy@eagle.org");
    // c4.Print(); cout << endl;
    return 0;
}
```

这里`PhoneContact`和`EmailContact`是mixin类，可以简单地与`Customer`组合，但`PhoneContace<EmailContact<Customer>>`或反过来都不行，因为构造函数的参数数量不对。

最糟糕的解法是限制或改变mixin类组合的顺序，但也解不了这里的问题。我们讨论4种部分解法，它们某种角度会更好，但仍然会有诸如不必要的开销、调用代码臃肿、新增mixin不方便等问题。但这些解法也能帮助我们去理解更高级、更完善的解法。

## 部分解法

前述方法的问题在于不存在有合适的构造函数的mixin类。一个简单方法就是再增加一个有这样的构造函数的mixin类型，这就需要用到多重继承了。但这种方法需要大幅修改已有代码。首先就是要用多重继承。为了避免产生多个`Customer`子对象，我们需要把`Customer`变成虚基类。之后`PhoneAndEmailContact`就需要去初始化`Customer`了。

```cpp
class Customer {
public:
    Customer(const char* fn, const char* ln)
        : mFirstName(fn), mLastName(ln) {}
    void Print() const {
        cout << mFirstName
            << ' '
            << mLastName;
    }
private:
    const char* mFirstName,
    const char* mLastName;
};
 
// The new mixin class will be defined using multiple inheritance. 
// Therefore Base must be turned into a virtual base class.
template <typename Base>
class PhoneContact: virtual public Base {
public:
    PhoneContact(const char* fn, const char* ln, const char* pn)
        :Base(fn, ln), mPhone(pn) {}
    void Print() const {
        Base::Print();
        BasicPrint();
    }
protected:
    // We need an "inner" print method that prints the PhoneContact-specific
    // information only.
    void BasicPrint() const {
        cout << ' ' << mPhone;
    }
private:
    const char* mPhone;
};
 
// Base has to be declared as virtual base class here, too.
template <typename Base>
class EmailContact: virtual public Base {
public:
    EmailContact(const char* fn, const char* ln, const char* e)
        : Base(fn, ln), mEmail(e){}
    void Print() const {
        Base::Print();
        BasicPrint();
    }
protected:
    // We need an "inner" print method that prints the EmailContact-specific
    // information only.
    void BasicPrint() const {
        cout << ' ' << mEmail;
    }
private:
    const char* mEmail;
};
 
template <typename Base>
class PhoneAndEmailContact :
    public PhoneContact<Base>,
    public EmailContact<Base> {
public:
    // Because Base is a virtual base class, PhoneAndEmailContact is now 
    // responsible for its initialization.
    PhoneAndEmailContact(const char* fn, const char* ln, char* pn, const char* e)
        : PhoneContact<Base>(fn, ln, pn)
        , EmailContact<Base>(fn, ln, e)
        , Base(fn, ln) {}
    void Print() const {
        Base::Print();
        PhoneContact<Base>::BasicPrint();
        EmailContact<Base>::BasicPrint();
    }
};
```

一个问题是`PhoneContact`和`EmailContact`的`Print`函数需要拆成`Print`和`BasicPrint`两个函数，后者不调用`Base::Print`，否则`PhoneAndEmailContact::Print`就会调用两次`Base::Print`，得不到想要的输出。如果我们还要增加新的mixin类型，比如`PostalAddress`，我们还要像这样再加3个mixin类型，随着功能的增加，我们需要组合出的mixin类型数量会以指数形式增加。

### 提供特定的参数类

一种方法是提供一个参数类，包含每个mixin需要的构造参数，这样不同的mixin都可以通过同样的参数对象来构造：

```cpp
// Define a class that wraps the union of all constructor arguments 
// of Customer and all derived mixin classes. 
 
// CustomerParameter combines all constructor arguments of CustomerParameter 
// and its derived mixin classes. The default values for the last two 
// arguments provide some convenience to the client programmer.
struct CustomerParameter {
    const char* fn,
    const char* ln,
    const char* pn,
    const char* e;
    CustomerParameter(
        const char* fn_,
        const char*ln_,
        const char* pn_ = "",
        const char* e_ = "")
        : fn(fn_), ln(ln_), pn(pn_), e(e_) {}
};
 
class Customer {
public:
    explicit Customer(const CustomerParameter& cp)
        : mFirstName(cp.fn), mLastName(cp.ln) {}
    void Print() const {
        cout << mFirstName
            << ' '
            << mLastName;
    }
private:
    const char* mFirstName,
    const char* mLastName;
};
 
template <typename Base>
class PhoneContact: public Base {
public:
    explicit PhoneContact(const CustomerParameter& cp)
        : Base(cp), mPhone(cp.pn) {}
    void Print() const {
        Base::Print();
        cout << ' ' << mPhone;
    }
private:
    const char* mPhone;
};
 
template <typename Base>
class EmailContact: public Base {
public:
    explicit EmailContact(const CustomerParameter& cp)
        : Base(cp), mEmail(cp.e) {}
    void Print() const {
        Base::Print();
        cout << ' ' << mEmail;  
    }
private:
    const char* mEmail;
};
```

这种方法也有问题。每当有新mixin类型时，我们都可能需要修改参数类。幸好已有的mixin类型不会受影响，但调用处在构造参数对象时可能要增加一个根本用不上的参数。

这种方法允许交换组合顺序，`PhoneContact<EmailContact<Customer>>`和`PhoneContact<EmailContact<Customer>>`都可以，但`Print`的行为不一样。

### 提供Init方法

另一种方法是两段构造，为每个类型增加一个Init方法，这样你就背离了通用的C++准则：对象只通过构造函数初始化。这样每个类型只需要有默认构造函数，构造完成后再由用户显式调用Init函数：

```cpp
// Define special intialization methods in each class and no longer rely 
// on the proper initialization though constructors.
 
class Customer {
public:
    // Initialization method for Customer.
    // A default constructor will be generated automatically.
    void init(const char* fn, const char* ln) {
        mFirstName = fn;
        mLastName = ln;
    }
    void Print() const {
        cout << mFirstName
            << ' '
            << mLastName;
    }
private:
    const char* mFirstName,
    const char* mLastName;
};
 
template <typename Base>
class PhoneContact: public Base {
public:
    // Initialization method for PhoneContact only.
    // A default constructor will be generated automatically.
    void init(const char* pn) {
        mPhone = pn;
    }

    void Print() const {
        Base::Print();
        cout << ' ' << mPhone;
    }
private:
    const char* mPhone;
};
 
template <typename Base>
class EmailContact: public Base {
public:
    // Initialization method for EmailContact only.
    // A default constructor will be generated automatically.
    void init(const char* e) {
        mEmail = e;
    }

    void Print() const {
        Base::Print();
        cout << ' ' << mEmail;
    }
private:
    const char* mEmail;
};
```

这种方法很容易出错，因为它把正确构造的责任推给了用户。而且调用Init时还要带上类型名，太丑了。

### 额外定义构造函数

这种方法是为mixin增加额外的构造函数，来满足所有可能的基类的构造需求。因为mixin类型都是模板类，而模板类中没人使用的方法是不会被产生出来的，因此这些新增加的构造函数在没人用时不会增加目标代码的体积。

```cpp
template <typename Base>
class PhoneContact: public Base {
public:
    // The following constructors will be instantiated only if required.
    PhoneContact(
        const char* fn,
        const char* ln,
        const char* pn)
        : Base(fn, ln), mPhone(pn) {}
    PhoneContact(
        const char* fn,
        const char* ln,
        const char* pn,
        const char* e)
        :Base(fn, ln, e), mPhone(pn) {}
    void Print() const {
        Base::Print();
        cout << ' ' << mPhone;
    }
private:
    const char* mPhone;
};
 
template <typename Base>
class EmailContact: public Base {
public:
    // The following constructors will be instantiated only if required.
    EmailContact(
        const char* fn,
        const char* ln,
        const char* e)
        : Base(fn, ln), mEmail(e) {}
    EmailContact(
        const char* fn,
        const char* ln,
        const char* pn,
        const char* e)
        : Base(fn, ln, pn), mEmail(e) {}
    void Print() const {
        Base::Print();
        cout << ' ' << mEmail;
    }
private:
    const char* mEmail;
};
```

不得不说这种方法就是维护性的噩梦。且这种方法对构造顺序有要求，即我们要多个构造函数，才能同时允许`PhoneContact<EmailContact<Customer>>`和`PhoneContact<EmailContact<Customer>>`。

## 设计新方法

首先，提供一个参数类是个好方法，为不同类型的构造提供了统一的接口。但应该做到不同的mixin类型有不同的参数类。

其次，调用处应该像往常一样声明对象。

### 异构值列表

C++对象构造时要从最上层的基类开始，按继承顺序依次构造，直到最下层的派生类型。因此，基类的构造参数是派生类构造参数的一个子集。想象我们提供一个构造参数的列表，每个构造函数都从列表头部拿走自己想要的参数，再将剩下的列表传给它的基类构造器，直到列表为空。这里需要列表能容纳不同类型的对象，每个元素都可以有不同的类型。

一种实现这样的异构列表的方法是递归地实例化一个模板列表类型：

```cpp
struct NIL {};
template <typename Head_, typename Tail_ = NIL>
struct List {
    typedef Head_ Head;
    typedef Tail_ Tail;

    List(const Head& h, const Tail& t = NIL())
        : mHead(h), mTail(t) {}

    Head mHead;
    Tail mTail;
};

List<signed char, List<short, List<int, List<long>>>>l(
    'a',
    List<short, List<int, List<long>>>(
        1,
        List<int, List<long>>(
            2,
            List<Long>(3))));
```

### 配置仓库与参数适配器

接下来是组合两个不同的方法。第一个是用traits类作为配置仓库。配置仓库允许你将mixin类型的实现与它的构造参数分离。每个mixin类型的构造参数就是它对应的配置仓库。基础类型，也就是`Customer`，变成模板类，模板参数就是它的配置类型，再用这个配置类型来声明它的参数类型。而派生的mixin类型则使用基类的配置类型与参数类型来声明自己的参数类型。这样，配置仓库就可以沿着继承链一直走到底，每个mixin类型都可以从中获取自己想要的参数：

```cpp
template <typename Config_>
class Customer {
public:
    typedef Config_ Config;
    typedef List<
        typename Config::LastnameType,
        List<typename Config::FirstnameType>> ParamType;
    explicit Customer(const ParamType& p)
        : mFirstName(p.mTail.mHead), mLastName(p.mHead) {}
    void Print() const {
        cout << mFirstName
            << ' '
            << mLastName;
    }
private:
    typename Config::FirstnameType mFirstName;
    typename Config::LastnameType mLastName;
};

template <typename Base>
class PhoneContact: public Base {
public:
    typedef typename Base::Config Config;
    typedef List<
        typename Config::PhoneNoType,
        typename Base::ParamType> ParamType;
    explicit PhoneContact(const ParamType& p)
        :Base(p.mTail), mPhone(p.mHead) {}
    void Print() const {
        Base::Print();
        cout << ' ' << mPhone;
    }
private:
    typename Config::PhoneNoType mPhone;
};

template <typename Base>
class EmailContact: public Base {
public:
    typedef typename Base::Config Config;
    typedef List<
        typename Config::EmailAddressType,
        typename Base::ParamType> ParamType;
    explicit EmailContact(const ParamType& p)
        :Base(p.mTail), mEmail(p.mHead) {}
    void Print() const {
        Base::Print();
        cout << ' ' << mEmail;
    }
private:
    typename Config::EmailAddressType mEmail;
};

template <typename Base>
struct ParameterAdapter: Base {
    typedef typename Base::Config Config;

    typedef typename Config::RET::ParamType P;
    typedef typename P::N P1;
    typedef typename P1::N P2;

    template <typename A1>
    explicit ParameterAdapter(const A1& a1) 
        : Base(a1) {}

    template <typename A1, typename A2>
    ParameterAdapter(const A1& a1, const A2& a2)
        : Base(P(a2,a1)) {}

    template <typename A1, typename A2, typename A3>
    ParameterAdapter(const A1& a1, const A2& a2, const A3& a3)
        : Base(P(a3,P1(a2,a1))) {}

    template <typename A1, typename A2, typename A3, typename A4>
    ParameterAdapter(
        const A1& a1,
        const A2& a2,
        const A3& a3,
        const A4& a4)
        : Base(P(a4,P1(a3,P2(a2,a1)))) {}
};
 
struct C1 {
    typedef C1 ThisConfig;

    typedef const char* FirstnameType;
    typedef const char* LastnameType;

    typedef Customer<ThisConfig> CustomerType;

    typedef ParameterAdapter<CustomerType> RET;
};
 
struct C2 {
    typedef C2 ThisConfig;

    typedef const char* FirstnameType;
    typedef const char* LastnameType;
    typedef const char* PhoneNoType;

    typedef Customer<ThisConfig> CustomerType;
    typedef PhoneContact<CustomerType> PhoneContactType;

    typedef ParameterAdapter<PhoneContactType> RET;
};
 
struct C3 {
    typedef C3 ThisConfig;

    typedef const char* FirstnameType;
    typedef const char* LastnameType;
    typedef const char* EmailAddressType;

    typedef Customer<ThisConfig> CustomerType;
    typedef EmailContact<CustomerType> EmailContactType;

    typedef ParameterAdapter<EmailContactType> RET;
};

struct C4 {
    typedef C4 ThisConfig;

    typedef const char* FirstnameType;
    typedef const char* LastnameType;
    typedef const char* PhoneNoType;
    typedef const char* EmailAddressType;

    typedef Customer<ThisConfig> CustomerType;
    typedef PhoneContact<CustomerType> PhoneContactType;
    typedef EmailContact<PhoneContactType> EmailContactType;

    typedef ParameterAdapter<EmailContactType> RET;
};
```

这里`C1`、`C2`、`C3`、`C4`就是不同的配置仓库，而它们的`RET`则是组合了不同mixin的目标类型。用法为：

```cpp
C1::RET c1("Teddy","Bear");
c1.Print(); cout << endl;
C2::RET c2("Rick","Racoon","050-998877");
c2.Print(); cout << endl;
C3::RET c3("Dick","Deer","dick@deer.com");
c3.Print(); cout << endl;
C4::RET c4("Eddy","Eagle","049-554433","eddy@eagle.org");
c4.Print(); cout << endl;
```

`ParameterAdapter`的作用是提供一种通用的构造接口，使用C++11的变长模板会让它的实现更简洁，功能更强大。

通过结合使用这两种方法，我们得到了一种简洁的，统一的mixin构造方式。这种构造方式符合日常用法，只需要调用者关心自己用到的mixin类型，且是类型安全的。

