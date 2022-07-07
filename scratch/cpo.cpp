#include <iostream>
#include <type_traits>

namespace standard {
   namespace detail
   {
      void tag_invoke();
      struct tag_invoke_t 
      {
         template<typename Tag, typename... Args>
         constexpr auto operator() (Tag tag, Args &&... args) const
            noexcept(noexcept(tag_invoke(static_cast<Tag &&>(tag), static_cast<Args &&>(args)...)))
            -> decltype(tag_invoke(static_cast<Tag &&>(tag), static_cast<Args &&>(args)...))
         {
            return tag_invoke(static_cast<Tag &&>(tag), static_cast<Args &&>(args)...);
         }
      };
   }

   inline constexpr detail::tag_invoke_t tag_invoke{};
   
   template<auto& Tag> 
   using tag_t = std::decay_t<decltype(Tag)>; 
}

namespace standard {

   namespace detail {
      struct do_something_t {
         template<typename T>
         void operator()(T &t) noexcept {
            tag_invoke(do_something_t{}, t);
         }
      };

      // 注意函数定义不再是do_something，而是tag_invoke，tag就是 detail::do_something_t
      template<typename T>
      void tag_invoke(do_something_t, T &t) noexcept {
         std::cout << "standard do something" << std::endl;
      }
   }

   inline detail::do_something_t do_something{};
}

namespace thirdparty {
  struct complicated_structure {
    //....各种指针，引用

    //tag_t<do_something>就是standard::detail::do_something_t
  };

    void tag_invoke(standard::tag_t<standard::do_something>, complicated_structure &t) noexcept {
       std::cout << "customized do something" << std::endl;
    }

  struct simple_structure {
  };
}

int main()
{
   thirdparty::simple_structure s;
   standard::do_something(s);

   thirdparty::complicated_structure c;
   standard::do_something(c);

   using namespace standard;
   do_something(s);
   do_something(c);

   return 0;
}
