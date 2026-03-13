#include "./Presenter.h"
#include "ModuleView.h"
#include "InterfaceObject.h"
#include "Module_Info.h"

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

	int toPinDirection = toType->plugs[toPin]->GetDirection();
	int fromPinDirection = fromType->plugs[fromPin]->GetDirection();

	return fromPinDirection != toPinDirection;
}
