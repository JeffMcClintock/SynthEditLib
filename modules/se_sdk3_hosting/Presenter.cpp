#include "./Presenter.h"
#include "ModuleView.h"
#include "InterfaceObject.h"
#include "module_info.h"

bool PresenterBase::CanConnect(SE2::CableType cabletype, int32_t fromModule, int fromPin, int32_t toModule, int toPin)
{
	if (cabletype != SE2::CableType::PatchCable)
		return false;

	auto fromUg = HandleToObject(fromModule);
	auto toUg = HandleToObject(toModule);
	if (fromUg == nullptr || toUg == nullptr)
		return false;

	auto fromType = fromUg->getModuleType();
	auto toType = toUg->getModuleType();

	if (fromType == nullptr || toType == nullptr)
		return false;

	// Pin indices arrive unvalidated. Module_Info::plugs is a std::map keyed by pin
	// id, so plugs[pin] on an unknown pin inserts a null entry into the shared module
	// description and then returns it to be dereferenced; getPinDescriptionById does
	// the same lookup with no insert and a null return.
	auto fromPinDesc = fromType->getPinDescriptionById(fromPin);
	auto toPinDesc = toType->getPinDescriptionById(toPin);

	if (fromPinDesc == nullptr || toPinDesc == nullptr)
		return false;

	int toPinDirection = toPinDesc->GetDirection();
	int fromPinDirection = fromPinDesc->GetDirection();

	return fromPinDirection != toPinDirection;
}
