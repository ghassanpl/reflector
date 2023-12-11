#include "../Include/ReflectorClasses.h"

#define RClass()
#define RBody()
#define RField()

struct ILogger{};

RClass();
struct Game
{
    RBody();

    RField(InjectDependency = ''); /// [Name = ""], 
    ILogger* Logger = nullptr;
};