#pragma once
#include <memory>
#include <string>
namespace Ship {
struct ResourceInitData { std::string Path; };
class IResource { public: virtual ~IResource() = default; };
template<class T> class Resource : public IResource {
public: explicit Resource(std::shared_ptr<ResourceInitData>) {}
};
}
