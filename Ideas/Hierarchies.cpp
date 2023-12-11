#if 0
#include "../Include/ReflectorClasses.h"
#include <memory>
using namespace std;
using namespace Reflector;
template <typename T>
using uptr = std::unique_ptr<T>;

#define RClass(...)
#define RField(...)
#define RMethod(...)
#define RBody(...) \
    template <typename T> \
    T* GetParent() {} \
    Game* GetRoot() {} \
    Reflectable* mParent = nullptr; \

template <typename T>
Reflectable* ResolvePathIn(std::string_view path, T child_range);
Reflectable* ResolvePathIn(std::string_view path, Reflectable* child);

RClass(InHierarchy, Root);
class Game {
    RBody();
        Reflectable* Get(std::string_view path);
        auto SearchLocationsByName(std::string_view name) {}
        auto SearchAchievementsByName(std::string_view name) {}
        std::string ValidateChildName(std::string_view childset, std::string_view new_name, std::string_view old_name = {});

    RField(Path="/Locations/");
    vector<uptr<Location>> Locations;

    RField(Path="/Achievements/");
    vector<uptr<Achievement>> Achievements;

    RMethod(ValidatorFor="Achievements")
    std::string ValidateAchievementName(std::string_view new_name, std::string_view old_name = {});
};

RClass(InHierarchy);
class Location {
    RBody();
        Reflectable* Get(std::string_view path);
        auto ResolveChildByName(std::string_view name) {}
        auto SearchObjectsByName(std::string_view name) {}
        std::string ValidateChildName(std::string_view childset, std::string_view new_name, std::string_view old_name = {});

    RField(Key);
    string Name;

    RField(Path="/");
    vector<uptr<Object>> Objects;
};

RClass(InHierarchy, Leaf);
class Achievement
{
    RBody();

};

RClass(InHierarchy);
class Object
{
    RBody();
        Reflectable* Get(std::string_view path);
        std::string ValidateChildName(std::string_view childset, std::string_view new_name, std::string_view old_name = {});

    RField(Path="/");
    vector<uptr<Object>> Objects;

    RField(Path=":");
    vector<uptr<Component>> Components;
};

RClass(InHierarchy, Leaf);
class Component
{
    RBody();

};
#endif
