#include <iostream>
#include <yui/core/VNode.hpp>
#include <yui/core/Props.hpp>

int main() {
    using namespace yui;
    std::cout << "sizeof(LayoutProps) = " << sizeof(LayoutProps) << std::endl;
    std::cout << "sizeof(EventProps) = " << sizeof(EventProps) << std::endl;
    std::cout << "sizeof(BoxProps) = " << sizeof(BoxProps) << std::endl;
    std::cout << "sizeof(TextProps) = " << sizeof(TextProps) << std::endl;
    std::cout << "sizeof(InputProps) = " << sizeof(InputProps) << std::endl;
    std::cout << "sizeof(PropsVariant) = " << sizeof(PropsVariant) << std::endl;
    std::cout << "sizeof(VNode) = " << sizeof(VNode) << std::endl;
    std::cout << "sizeof(Child) = " << sizeof(Child) << std::endl;
    std::cout << "sizeof(Component) = " << sizeof(Component) << std::endl;
    std::cout << "sizeof(std::function<void()>) = " << sizeof(std::function<void()>) << std::endl;
    std::cout << "sizeof(std::optional<float>) = " << sizeof(std::optional<float>) << std::endl;
    std::cout << "sizeof(std::string) = " << sizeof(std::string) << std::endl;
    std::cout << "sizeof(std::vector<Child>) = " << sizeof(std::vector<Child>) << std::endl;
    return 0;
}
