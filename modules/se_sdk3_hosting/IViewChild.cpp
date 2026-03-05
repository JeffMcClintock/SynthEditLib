
#include "IViewChild.h"
#include "ViewBase.h"

namespace SE2
{
	ViewChild::ViewChild(Json::Value* pDatacontext, ViewBase* pParent) : parent(pParent)
		, datacontext(pDatacontext)
	{
		handle = (*datacontext)["handle"].asInt();
	}

	IPresenter* ViewChild::Presenter()
	{
		return parent->Presenter();
	}

	bool ViewChild::editEnabled()
	{
		return Presenter()->editEnabled();
	}

	bool ViewChild::imCaptured()
	{
		return parent->isCaptured(this);
	}

	ViewChild::~ViewChild()
	{
		assert(!parent || parent->mouseOverObject != this);
	}

	gmpi::drawing::Factory ViewChild::getFactory()
	{
		gmpi::drawing::Factory factory;
		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		parent->getDrawingFactory(unknown.put());
		unknown->queryInterface(&gmpi::drawing::api::IFactory::guid, (void**)gmpi::drawing::AccessPtr::put(factory));
		return factory;
	}

}