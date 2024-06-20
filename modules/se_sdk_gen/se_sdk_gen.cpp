// se_sdk_gen.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "windows.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <assert.h>
#include "../tinyxml2/tinyxml2.h"
#include "copyright.h"
#include "errhandlingapi.h"

std::string ptrToString(const char* s)
{
	if (s)
		return s;
	return {};
}

using namespace tinyxml2;
/*
	// Music plugin audio processing interface.
	class IMpAudioPlugin : public IMpUnknown
	{
	public:
		// Establish connection to host.
		virtual int32_t setHost(IMpUnknown* host) = 0;

		// Processing about to start.  Allocate resources here.
		virtual int32_t open() = 0;

		// Notify plugin of audio buffer address, one pin at a time. Address may change between process() calls.
		virtual int32_t setBuffer(int32_t pinId, float* buffer) = 0;

		// Process a time slice. No return code, must always succeed.
		virtual void process(int32_t count, const MpEvent* events) = 0;
	};
*/

//PROBLEM, can't put pointers in xml, e.g. <float*
// change to <var type="float*" Name="whatever

// Note: never return a small struct across the ABI. MSC++ is not compatible with C in this case.

enum class EType { Unknown, Interface, Struct, Enum, Flags };
enum class EArgType { In, Mutable, Out };

struct environment
{
	std::ostringstream& oss;
	XMLElement* parent = {};
	int indent = 0;
};

EType typeFromString(std::string s)
{
	if (s =="struct")
	{
		return EType::Struct;
	}
	if (s =="interface")
	{
		return EType::Interface;
	}
	else if (s == "enum")
	{
		return EType::Enum;
	}
	else if (s == "flags")
	{
		return EType::Flags;
	}

	return EType::Unknown;
}

std::string camelCase(std::string s)
{
	std::string ret;
	bool first{ false };
	const char* c = s.c_str();
	while (*c)
	{
		if (*c == ' ')
		{
			++c;
			first = true;
			continue;
		}

		if (first)
		{
			ret.push_back(std::toupper(*c));
		}
		else
		{
			ret.push_back(std::tolower(*c));
		}

		first = false;
		++c;
	}

	return ret;
}

std::string PascalCase(std::string s)
{
	std::string ret;
	bool first{ true };
	const char* c = s.c_str();
	while (*c)
	{
		if (*c == ' ')
		{
			++c;
			first = true;
			continue;
		}

		if (first)
		{
			ret.push_back(std::toupper(*c));
		}
		else
		{
			ret.push_back(std::tolower(*c));
		}

		first = false;
		++c;
	}

	return ret;
}


std::string ALL_CAPS(std::string s)
{
	std::replace_if(std::begin(s), std::end(s),
		[](std::string::value_type v) { return v == ' '; },
		'_');

	std::transform(s.begin(), s.end(), s.begin(), std::toupper);

	return s;
}

struct lang_base
{
	inline static std::vector< std::pair<std::string, EType> > typeNames;

	static EType typeOf(std::string name)
	{
		if (const bool isPointer = !name.empty() && name.back() == '*'; isPointer)
		{
			name.pop_back();
		}

		for (auto& it : typeNames)
		{
			if (it.first == name)
				return it.second;
		}
		return EType::Unknown;
	}

	bool isSmallStruct(std::string_view name)
	{
		return name.find("point") == 0 || name.find("size") == 0;
	}

	// "int cat[8]" => "[8]"
	std::string getArrayString(std::string s)
	{
		if (s.size() > 1 && s[0] == 'u' && isdigit(s[1])) // u32, u16, u8
		{
			std::string arr; // e.g. "[3]";
			if (auto p = s.find_first_of('['); p != std::string::npos)
			{
				arr = s.substr(p);
				s = s.substr(0, p);
			}
			return arr;
		}
		return {};
	}

	std::string nativeVariableName(std::string s, bool isOutParameter = false)
	{
		if (const bool isPointer = !s.empty() && s.back() == '*'; isPointer)
		{
			s.pop_back();
		}

		if (isOutParameter)
			return camelCase("return " + s);

		return camelCase(s);
	}
};

struct lang_cpp : public lang_base
{
	std::string formatedName(std::string s, EType type)
	{
		switch (type)
		{
		case EType::Interface:
		{
			if (s == "unknown")
				return "gmpi::api::I" + PascalCase(s);
			const std::string namespaceClassPrefix{ "I" };
			return namespaceClassPrefix + PascalCase(s);
		}
		break;

		case EType::Flags:
		case EType::Enum:
		case EType::Struct:
		{
			//if( s == "return code" )
			//	return "gmpi::" + PascalCase(s);
			return PascalCase(s);
		}
		break;
		};

		assert(false);
		return s;
	}

	std::string nativeType(std::string s, std::string name = {})
	{
		const auto t = typeOf(s);

		if (t != EType::Unknown)
			return formatedName(s, t);

		if (s =="int")
		{
			return "int32_t";
		}
		else if (s == "bool")
		{
			return s;
		}
		else if (s == "char_ptr")
		{
			return "const char*";
		}
		else if (s =="event*")
		{
			return "Event*";
		}
		else if (s == "void")
		{
			return s;
		}
		else if (s =="float*")
		{
			return "float*";
		}
		else if (s.size() > 1 && s[0] == 'u' && isdigit(s[1])) // u32, u16, u8
		{
			std::string arr; // e.g. "[3]";
			if (auto p = s.find_first_of('['); p != std::string::npos)
			{
				arr = s.substr(p);
				s = s.substr(0, p);
			}

			std::string pointerPostfix;
			if (const bool isPointer = !s.empty() && s.back() == '*'; isPointer)
			{
				s.pop_back();
				pointerPostfix = "*";
			}

			return "uint" + s.substr(1) + "_t" + pointerPostfix;
		}
		else if (s == "enum" || s == "flags")
		{
			return PascalCase(name);
		}

		return s;
	}

	std::string makeArgType(std::string s, EArgType argType, std::string arg_namespace = {})
	{
		const auto t = typeOf(s);
		const bool isPointer = s.size() > 1 && s.back() == '*';
		const bool smallStructOptimisation = !isPointer && isSmallStruct(s);

		if (s == "point*")
			int i = 9;

		if (t == EType::Struct && !smallStructOptimisation)
		{
			if (argType == EArgType::Out)
				return arg_namespace + formatedName(s, t) + "*";

			return "const " + arg_namespace + formatedName(s, t) + (isPointer ? "" : "*");
		}
		if (t == EType::Interface)
		{
			// exception, strings are passed in as 'const char*' (but out as IString*).
			if ("string" == s)
			{
				if (argType == EArgType::Out)
					return formatedName(s, t) + "*";
				return "const char*";
			}

			if (argType == EArgType::Out)
				return arg_namespace + formatedName(s, t) + "**";

			return arg_namespace + formatedName(s, t) + '*';
		}

		if (argType == EArgType::Out)
			return arg_namespace + nativeType(s) + '*';

		if(isPointer && argType != EArgType::Mutable)
			return "const " + arg_namespace + nativeType(s);

		return arg_namespace + nativeType(s);
	}

	const char* interfaceReturnType = "return code";

	struct include_guard
	{
		include_guard(environment env)
		{
			env.oss << "#pragma once\n\n";
		}
	};
};


struct lang_c : public lang_base
{
	inline static std::string namespace_;
	inline static std::vector<std::string> IUnknownMethods;

	std::string formatedName(std::string s, EType type)
	{
		switch (type)
		{
		case EType::Interface:
		{
			const std::string namespaceClassPrefix{ "GMPI_I" };
			return namespaceClassPrefix + PascalCase(s);
		}
		break;

		case EType::Flags:
		case EType::Enum:
		case EType::Struct:
		{
			const std::string namespaceClassPrefix{ "GMPI_" };
			return namespaceClassPrefix + PascalCase(s);
		}
		break;
		};

		assert(false);
		return s;
	}
	
	std::string nativeType(std::string s)
	{
		const auto t = typeOf(s);

		if (t != EType::Unknown)
			return formatedName(s, t);

		if (s == "int")
		{
			return "int32_t";
		}
		else if (s == "string") // UTF8* String, may be passed as a 'const char*' or returned as IString
		{
			return "char*";
		}
		else if (s == "void")
		{
			return s;
		}
		else if (s == "char*") // always a char*, not always a string.
		{
			return "char*";
		}
		else if (s == "bool")
		{
			return s;
		}
		else if (s == "float")
		{
			return s;
		}
		else if (s == "float*")
		{
			return s;
		}
		else if (s == "void*")
		{
			return s;
		}
		else if (s == "enum" || s == "flags")
		{
			return "int32_t";
		}
		else if (s.size() > 1 && s[0] == 'u' && isdigit(s[1])) // u32, u16, u8
		{
			const bool isPointer = s.back() == '*';
			
			if(isPointer)
				s = s.substr(0, s.size() - 1);

			std::string arr; // e.g. "[3]";
			if (auto p = s.find_first_of('['); p != std::string::npos)
			{
				arr = s.substr(p);
				s = s.substr(0, p);
			}
			return "uint" + s.substr(1) + "_t" + (isPointer ? "*" : "");
		}
		else // assume it's a struct
		{
			return "struct " + nativeStructName(s);
		}

		return s;
	}

	std::string makeArgType(std::string s, EArgType argType)
	{
		const auto t = typeOf(s);

		std::string nativeTypename;

		switch (t)
		{
		case EType::Struct:
		{
			if (argType == EArgType::Out)
				return formatedName(s, t) + "**";

			return "const " + formatedName(s, t) + '*';
		}
		break;

		case EType::Interface:
		{
			// exception, strings are passed in as 'const char*' (but out as IString*).
			if ("string" == s)
			{
				if (argType == EArgType::Out)
					return formatedName(s, t) + "*";
				return "const char*";
			}

			if (argType == EArgType::Out)
				return formatedName(s, t) + "**";

			return formatedName(s, t) + '*';
		}
		break;

		case EType::Flags:
		case EType::Enum:
		{
			nativeTypename = "int32_t";
		}
		break;

		default:
		{
			nativeTypename = nativeType(s);
		}
		break;
		};

		const bool isPointer = s.size() > 1 && s.back() == '*';

		if (argType == EArgType::Out)
		{
			nativeTypename += '*';
		}
		else
		{
			if (isPointer && argType != EArgType::Mutable)
				nativeTypename = "const " + nativeTypename;
		}

		return nativeTypename;
	}

	std::string nativeStructName(std::string s)
	{
		return namespace_ + PascalCase(s);
	}

	const char* interfaceReturnType = "int32_t";

	struct include_guard
	{
		environment& env_;

		include_guard(environment env) : env_(env)
		{
			env.oss << "#ifndef GMPI_" << ALL_CAPS(env.parent->Name()) << "_H_INCLUDED\n#define GMPI_" << ALL_CAPS(env.parent->Name()) << "_H_INCLUDED\n\n";
		}

		~include_guard()
		{
			env_.oss << "\n#endif\n";
		}
	};
};

const char* indent(int level)
{
	const char* spaces = "                        "; // should be 24
	return spaces + (24 - 4 * level);
}

struct indenter
{
	environment& env_;
	std::string name_;
	std::string trailing_name_;

	indenter(environment env, std::string name, std::string trailing_name = {}) :
		env_(env),
		name_(name),
		trailing_name_(trailing_name)
	{
		env.oss << indent(env_.indent ) << name_ << "\n{\n";

		++env_.indent;
	}

	~indenter()
	{
		--env_.indent;
		env_.oss << indent(env_.indent) << "}";
		if (!trailing_name_.empty())
		{
			env_.oss << ' ' << trailing_name_;
		}
		if (name_.find("namespace") != std::string::npos)
		{
			env_.oss << " // namespace";
		}
		else
		{
			env_.oss << ';';
		}
		env_.oss << "\n";
	}
};

struct aligner
{
	environment& env_;
	std::vector<std::string> lines;

	aligner(environment env) :
		env_(env)
	{
	}

	~aligner()
	{
		static const std::string spaces{ "                                                         " };

		{
			int maxEq = -1;
			for (const auto& line : lines)
			{
				maxEq = (std::max)(maxEq, (int)line.find_first_of('='));
			}

			for (auto& line : lines)
			{
				const auto p = line.find_first_of('=');
				if (p != std::string::npos)
				{
					const int spacing = (std::max)(0, maxEq - (int)p);
					line = line.substr(0, p) + spaces.substr(0, spacing) + line.substr(p);
				}
			}
		}

		{
			int maxslashes = -1;
			for (const auto& line : lines)
			{
				maxslashes = (std::max)(maxslashes, (int)line.find_first_of('/'));
			}
			for (auto& line : lines)
			{
				const auto p = line.find_first_of('/');
				if (p != std::string::npos)
				{
					const int spacing = (std::max)(0, maxslashes - (int)p);
					line = line.substr(0, p) + spaces.substr(0, spacing) + line.substr(p);
				}
			}
		}

		for (const auto& line : lines)
		{
			env_.oss << line << "\n";
		}
	}
};

struct printer_struct_cpp : public lang_cpp
{
	void accept(environment env)
	{
		const auto struct_name = PascalCase(env.parent->Attribute("name"));
		indenter s(env, "struct " + struct_name);

		aligner aln(env);
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const std::string memberType = e->Name();

			if (memberType == "var")
			{
				const auto datatype = e->Attribute("type");
				std::string name(e->Attribute("name"));
				const auto com = e->Attribute("comment");
				std::string defaultval = ptrToString(e->Attribute("default"));

				const auto arg_classification = typeOf(datatype);
				std::string arg_initializer;
				if (EType::Unknown == arg_classification)
				{
					arg_initializer = "{" + defaultval + "}";
				}

				// can't have a pure numeric name in C++
				if (isdigit(name[0]))
				{
					name = "_" + name;
				}

				std::string line = indent(env.indent + 1) + nativeType(datatype, name) + " " + nativeVariableName(name) + getArrayString(datatype) + arg_initializer + ";";

				if (com)
				{
					line += " // " + std::string(com);
				}
				aln.lines.push_back(line);
			}
			else
			{
				if (memberType == "method")
				{
					int i = 9;
					// TODO
				}
				else
				{
					assert(false); // ???
				}
			}
		}
	}
};

struct printer_struct_c : public lang_c
{
	void accept(environment env)
	{
		const auto struct_name = nativeStructName(env.parent->Attribute("name"));
		indenter s(env, "typedef struct " + struct_name, struct_name);

		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const auto datatype = e->Attribute("type");
			const auto name = e->Attribute("name");

			env.oss << indent(env.indent + 1) << nativeType(datatype) << " " << nativeVariableName(name) << getArrayString(datatype) << ";\n";
		}
	}
};

void print_guid(environment env, const char* g)
{
	if (!g)
		return;

	const auto GUID = std::string(g);

	env.oss
		<< "{ 0x" << GUID.substr(0, 8)
		<< ", 0x" << GUID.substr(9, 4)
		<< ", 0x" << GUID.substr(14, 4)
		<< ", { 0x" << GUID.substr(19, 2)
		<< ", 0x" << GUID.substr(21, 2);

	for (int i = 24; i < 35; i += 2)
		env.oss << ", 0x" << GUID.substr(i, 2);

	env.oss << "} };\n";
};

/*
	<interface name="audio plugin" base="unknown">
		<method name="set host">
			<arg name="host" type="unknown"/>
		</method>
*/

struct argument
{
	std::string type;
	std::string name;
	std::string datatype;
};

struct printer_interface_cpp : public lang_cpp
{
	void accept(environment env)
	{
		const auto struct_name = env.parent->Attribute("name");
		const auto structName = formatedName(struct_name, EType::Interface);
		const auto base_name = env.parent->Attribute("base");

		env.oss << "// INTERFACE '" << structName << "'\n";
		env.oss << "struct DECLSPEC_NOVTABLE" << " " << structName;

		if (base_name)
		{
			env.oss << " : public " << formatedName(base_name, EType::Interface);
		}

		env.oss << "\n" << "{\n";

		// methods
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const auto datatype = e->Name();
			auto name_ptr = e->Attribute("name");
			const auto comments = e->Attribute("comment");
			
			// make methods less verbose. e.g. <out name="gradientstop collection" type="gradientstop collection"/>
			std::string name;
			if (name_ptr)
				name = name_ptr;
			else
				name = datatype;

			if(comments)
			{
				env.oss << indent(env.indent + 1) << "// " << comments << "\n";
			}

			// collect arguments
			std::vector< argument > args;
			for (auto arg = e->FirstChildElement(); arg; arg = arg->NextSiblingElement())
			{
				args.push_back(argument{ ptrToString(arg->Name()), ptrToString(arg->Attribute("name")), ptrToString(arg->Attribute("type")) });
			}

			// shorthand for a simple setter
			if (strcmp("set", datatype) == 0)
			{
				const auto arg_type = e->Attribute("type");
				auto arg_name = e->Attribute("name");
				if (!arg_name)
				{
					arg_name = arg_type;
				}
				name = "set " + std::string(arg_name);

				args.push_back(argument{ "in", ptrToString(arg_name), ptrToString(arg_type) });
			}
			if (strcmp("get", datatype) == 0)
			{
				const auto arg_type = e->Attribute("type");
				auto arg_name = e->Attribute("name");
				if (!arg_name)
				{
					arg_name = arg_type;
				}
				name = "get " + std::string(arg_name);

				args.push_back(argument{ "out", ptrToString(arg_name), ptrToString(arg_type) });
			}

			std::string returnType = interfaceReturnType;
			if (auto returnTypeS = e->Attribute("returnType") ; returnTypeS)
			{
				returnType = returnTypeS;
			}
			env.oss << indent(env.indent + 1) << "virtual " << makeArgType(returnType, EArgType::In) << " " << nativeVariableName(name) << "(";
			bool first = true;
			for (auto& arg : args)
			{
				if (!first)
				{
					env.oss << ", ";
				}

				EArgType argtype = EArgType::In;
				if (arg.type == "out")
				{
					argtype = EArgType::Out;
				}
				else if (arg.type == "mut")
				{
					argtype = EArgType::Mutable;
				}

				auto arg_name = arg.name;
				const bool isOutParameter = argtype == EArgType::Out;
				if (arg_name.empty())
				{
					arg_name = arg.datatype;
				}

				env.oss << makeArgType(arg.datatype, argtype) << ' ' << nativeVariableName(arg_name, isOutParameter);

				first = false;
			}

			env.oss << ") = 0;\n";
		}

		// print the GUID
		/*
			// GUID for IMpAudioPluginHost.
			// {87CCD426-71D7-414E-A9A6-5ADCA81C7420}
			static const MpGuid MP_AUDIO_PLUGIN_HOST = // !!! should this be MP_IID_AUDIO_PLUGIN_HOST ???!!! (and others)
			{ 0x87ccd426, 0x71d7, 0x414e, { 0xa9, 0xa6, 0x5a, 0xdc, 0xa8, 0x1c, 0x74, 0x20 } };
		*/
		{
			const auto g = env.parent->Attribute("guid");
			if (g)
			{
				env.oss
					<< "\n"
					<< indent(env.indent + 1) << "// {" << std::string(g) << "}\n"
					<< indent(env.indent + 1) << "inline static const gmpi::api::Guid guid =\n"
					<< indent(env.indent + 1);

				print_guid(env, g);
			}
		}

		// end class
		env.oss << "};\n";
	}
};

struct printer_interface_wrapper_cpp : public lang_cpp
{
	void accept(environment env)
	{
		const auto struct_name = env.parent->Attribute("name");
		const auto structName = PascalCase(struct_name);
		const auto base_name = env.parent->Attribute("base");

		env.oss << "class " << structName;

		if (base_name)
		{
			env.oss << " : public gmpi::IWrapper<api::" << structName << ">";
		}

		env.oss << "\n" << "{\n";

		// methods
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const auto datatype = e->Name(); // "method"
			auto name_ptr = e->Attribute("name");
			const auto comments = e->Attribute("comment");

			// make methods less verbose. e.g. <out name="gradientstop collection" type="gradientstop collection"/>
			std::string name;
			if (name_ptr)
				name = name_ptr;
			//else ??
			//	name = datatype;

			std::string returnType = "void";
			if (auto returnTypeS = e->Attribute("returnType"); returnTypeS)
			{
				returnType = returnTypeS; // primary return type is passed like an 'in' arg (from DAWs perspective).
			}
			if (comments)
			{
				env.oss << indent(env.indent + 1) << "// " << comments << "\n";
			}

			// collect arguments
			std::vector< argument > args;
			for (auto arg = e->FirstChildElement(); arg; arg = arg->NextSiblingElement())
			{
				args.push_back(argument{ ptrToString(arg->Name()), ptrToString(arg->Attribute("name")), ptrToString(arg->Attribute("type")) });
			}

			if (strcmp("set", datatype) == 0)
			{
				const auto arg_type = e->Attribute("type");
				auto arg_name = e->Attribute("name");
				if (!arg_name)
				{
					arg_name = arg_type;
				}
				name = "set " + std::string(arg_name);

				args.push_back(argument{ ptrToString("in"), ptrToString(arg_name), ptrToString(arg_type) });
			}

			if (strcmp("get", datatype) == 0)
			{
				returnType = e->Attribute("type");
				if(name.empty())
					name = "get " + returnType;

				std::string returnTypeString;
				if (typeOf(returnType) == EType::Interface)
				{
					returnTypeString = PascalCase(returnType); // wrapper type e.g. 'Graphics' not 'IGraphics'
				}
				else
				{
					returnTypeString = nativeType(returnType);
				}

				// FontMetrics getFontMetrics()
				env.oss << indent(env.indent + 1) << returnTypeString << " " << nativeVariableName(name) << "(";
			}
			else
			{
				env.oss << indent(env.indent + 1) << makeArgType(returnType, EArgType::In) << " " << nativeVariableName(name) << "(";
			}

			{
				bool first = true;
				for (auto& arg : args)
				{
					if (!first)
					{
						env.oss << ", ";
					}

					EArgType argtype = EArgType::In;
					if (arg.type == "out")
					{
						argtype = EArgType::Out;
					}
					else if (arg.type == "mut")
					{
						argtype = EArgType::Mutable;
					}

					auto arg_name = arg.name;
					const bool isOutParameter = argtype == EArgType::Out;
					if (arg_name.empty())
					{
						arg_name = arg.datatype;
					}

					const auto arg_classification = typeOf(arg.datatype);
//					std::string arg_namespace = (EType::Enum == arg_classification ? "gmpi::drawing::api::" : "");

					std::string arg_namespace;
					if (EType::Enum == arg_classification || EType::Struct == arg_classification || EType::Flags == arg_classification)
						arg_namespace = "gmpi::drawing::";
					else if (EType::Interface == arg_classification)
						arg_namespace = "gmpi::drawing::api::";

					env.oss << arg_namespace << makeArgType(arg.datatype, argtype) << ' ' << nativeVariableName(arg_name, isOutParameter);

					first = false;
				}
			}

			env.oss << ")\n";

			env.oss << indent(env.indent + 1) << "{\n";

			if (strcmp("set", datatype) == 0)
			{
				auto& arg = args.back();
				// get()->setTextAlignment(textAlignment);
				env.oss << indent(env.indent + 2) << "get()->" << nativeVariableName(name) << "(" << nativeVariableName(arg.name, false) << ");\n";
			}
			else if (strcmp("get", datatype) == 0)
			{
				std::string returnTypeString;
				if (typeOf(returnType) == EType::Interface)
				{
					returnTypeString = PascalCase(returnType); // wrapper type e.g. 'Graphics' not 'IGraphics'
				}
				else
				{
					returnTypeString = nativeType(returnType);
				}
				std::string nameString;

				if (typeOf(returnType) == EType::Interface)
				{
					// FontMetrics ret;
					env.oss << indent(env.indent + 2) << returnTypeString << " ret;\n";
					// get()->getFontMetrics(ret.put());
					env.oss << indent(env.indent + 2) << "get()->" << nativeVariableName(name) << "(ret.put());\n";
				}
				else
				{
					// int32_t ret{};
					env.oss << indent(env.indent + 2) << returnTypeString << " ret{};\n";
					// get()->getFontMetrics(&ret);
					env.oss << indent(env.indent + 2) << "get()->" << nativeVariableName(name) << "(&ret);\n";
				}
				env.oss << indent(env.indent + 2) << "return ret;\n";
			}
			else
			{
				env.oss << indent(env.indent + 2) << "get()->" << nativeVariableName(name) << "(";
				{
					bool first = true;
					for (auto& arg : args)
					{
						if (!first)
						{
							env.oss << ", ";
						}

						EArgType argtype = EArgType::In;
						if (arg.type == "out")
						{
							argtype = EArgType::Out;
						}
						else if (arg.type == "mut")
						{
							argtype = EArgType::Mutable;
						}

						auto arg_name = arg.name;
						const bool isOutParameter = argtype == EArgType::Out;
						if (arg_name.empty())
						{
							arg_name = arg.datatype;
						}

						env.oss << nativeVariableName(arg_name, isOutParameter);

						first = false;
					}
					env.oss << ");\n";
				}
			}

			env.oss << indent(env.indent + 1) << "}\n";
		}

		// end class
		env.oss << "};\n";
	}
};

struct printer_interface_platform_cpp : public lang_cpp
{
	void accept(environment env)
	{
		const auto struct_name = env.parent->Attribute("name");
		const std::string structName(PascalCase(struct_name));
		const auto base_name = env.parent->Attribute("base");

		const bool isDWriteInterface =
			structName.find("Text") != std::string::npos;

		const std::string DX_PREFIX = isDWriteInterface ? "DWRITE_" : "D2D1_";
		const std::string nativeInterfacePrefix = isDWriteInterface ? "IDWrite" : "ID2D1";

		env.oss << "class " << structName;

		if (base_name)
		{
			env.oss << " final : public GmpiDXWrapper<drawing::api::I" << structName << ", " << nativeInterfacePrefix << structName << ">";
		}

		env.oss << "\n" << "{\npublic:";

		// methods
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			env.oss << "\n";

			const auto datatype = e->Name(); // "method"
			auto name_ptr = e->Attribute("name");
			const auto comments = e->Attribute("comment");

			// make methods less verbose. e.g. <out name="gradientstop collection" type="gradientstop collection"/>
			std::string name;
			if (name_ptr)
				name = name_ptr;
			//else ??
			//	name = datatype;

			std::string returnType = interfaceReturnType;
			if (auto returnTypeS = e->Attribute("returnType"); returnTypeS)
			{
				returnType = returnTypeS; // primary return type is passed like an 'in' arg (from DAWs perspective).
			}
			if (comments)
			{
				env.oss << indent(env.indent + 1) << "// " << comments << "\n";
			}

			// collect arguments
			std::vector<argument> args;
			for (auto arg = e->FirstChildElement(); arg; arg = arg->NextSiblingElement())
			{
				args.push_back(argument{ ptrToString(arg->Name()), ptrToString(arg->Attribute("name")), ptrToString(arg->Attribute("type")) });
			}

			if (strcmp("set", datatype) == 0)
			{
				const auto arg_type = e->Attribute("type");
				auto arg_name = e->Attribute("name");
				if (!arg_name)
				{
					arg_name = arg_type;
				}
				name = "set " + std::string(arg_name);

				args.push_back(argument{ ptrToString("in"), ptrToString(arg_name), ptrToString(arg_type) });
			}

			if (strcmp("get", datatype) == 0)
			{
				std::string argName;
				if (name.empty())
				{
					argName = ptrToString(e->Attribute("type"));
				}
				else
				{
					argName = name;
				}
				std::string methodName = "get " + argName;

				args.push_back(argument{ ptrToString("out"), argName, ptrToString(e->Attribute("type")) });

				// FontMetrics getFontMetrics()
				env.oss << indent(env.indent + 1) << makeArgType(returnType, EArgType::In) << " " << nativeVariableName(methodName) << "(";
			}
			else
			{
				env.oss << indent(env.indent + 1) << makeArgType(returnType, EArgType::In) << " " << nativeVariableName(name) << "(";
			}

			{
				bool first = true;
				for (auto& arg : args)
				{
					if (!first)
					{
						env.oss << ", ";
					}

					EArgType argtype = EArgType::In;
					if (arg.type == "out")
					{
						argtype = EArgType::Out;
					}
					else if (arg.type == "mut")
					{
						argtype = EArgType::Mutable;
					}

					auto arg_name = arg.name;
					const bool isOutParameter = argtype == EArgType::Out;
					if (arg_name.empty())
					{
						arg_name = arg.datatype;
					}

					const auto arg_classification = typeOf(arg.datatype);
					std::string arg_namespace;
					if(EType::Enum == arg_classification || EType::Struct == arg_classification || EType::Flags == arg_classification)
						arg_namespace = "drawing::";
					else if(EType::Interface == arg_classification)
						arg_namespace = "drawing::api::";

					env.oss << makeArgType(arg.datatype, argtype, arg_namespace) << ' ' << nativeVariableName(arg_name, isOutParameter);

					first = false;
				}
			}

			env.oss << ") override\n";

			env.oss << indent(env.indent + 1) << "{\n";
#if 0
			if (strcmp("set", datatype) == 0)
			{
				auto& arg = args.back();
				// native()->setTextAlignment((DWRITE_TEXT_ALIGNMENT) textAlignment);

				env.oss << indent(env.indent + 2) << "const auto r = native()->" << nativeVariableName(name) << "((" << DX_PREFIX << ALL_CAPS(arg.name) << ") " << nativeVariableName(arg.name, false) << "); \n";
				env.oss << indent(env.indent + 2) << "return r == S_OK ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Fail;\n";
			}
			else
			if (strcmp("get", datatype) == 0)
			{
				std::string returnTypeString;
				if (typeOf(returnType) == EType::Interface)
				{
					returnTypeString = PascalCase(returnType); // wrapper type e.g. 'Graphics' not 'IGraphics'
				}
				else
				{
					returnTypeString = nativeType(returnType);
				}
				std::string nameString;

				if (typeOf(returnType) == EType::Interface)
				{
					// FontMetrics ret;
					env.oss << indent(env.indent + 2) << returnTypeString << " ret;\n";
					// get()->getFontMetrics(ret.put());
					env.oss << indent(env.indent + 2) << "const auto r = native()->" << nativeVariableName(name) << "(ret.put());\n";
					env.oss << indent(env.indent + 2) << "return r == S_OK ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Fail;\n";
				}
				else
				{
					// int32_t ret{};
					env.oss << indent(env.indent + 2) << returnTypeString << " ret{};\n";
					// get()->getFontMetrics(&ret);
					env.oss << indent(env.indent + 2) << "const auto r = native()->" << nativeVariableName(name) << "(&ret);\n";
					env.oss << indent(env.indent + 2) << "return r == S_OK ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Fail;\n";
				}
				env.oss << indent(env.indent + 2) << "return ret;\n";
			}
			else
#endif				
			{
				const bool checkReturnCode = returnType == "return code";
				const char* constAutoRetEquals = checkReturnCode ? "const auto r = " : "";
				env.oss << indent(env.indent + 2) << constAutoRetEquals << "native()->" << PascalCase(name) << "(";
				{
					bool first = true;
					for (auto& arg : args)
					{
						if (!first)
						{
							env.oss << ", ";
						}

						EArgType argtype = EArgType::In;
						if (arg.type == "out")
						{
							argtype = EArgType::Out;
						}
						else if (arg.type == "mut")
						{
							argtype = EArgType::Mutable;
						}

						auto arg_name = arg.name;
						const bool isOutParameter = argtype == EArgType::Out;
						if (arg_name.empty())
						{
							arg_name = arg.datatype;
						}

						const auto arg_classification = typeOf(arg.datatype);
						std::string arg_namespace;// = (EType::Enum == arg_classification ? "drawing::" : "");
						if (EType::Enum == arg_classification || EType::Struct == arg_classification || EType::Flags == arg_classification)
							arg_namespace = "drawing::";
						else if (EType::Interface == arg_classification)
							arg_namespace = "drawing::api::";

						std::string CAST_TO_DX = (EType::Struct == arg_classification || EType::Enum == arg_classification || EType::Flags == arg_classification) ?
							"(" + DX_PREFIX + ALL_CAPS(arg.datatype) + ") " : 
							"";

						env.oss << CAST_TO_DX << nativeVariableName(arg_name, isOutParameter);

						first = false;
					}
					env.oss << ");\n";
					if (checkReturnCode)
					{
						env.oss << indent(env.indent + 2) << "return r == S_OK ? ReturnCode::Ok : ReturnCode::Fail;\n";
					}
				}
			}

			env.oss << indent(env.indent + 1) << "}\n";
		}

		env.oss << "\n";

		env.oss << indent(env.indent + 1) << "GMPI_QUERYINTERFACE_NEW(drawing::api::I" << structName << ");\n";
		env.oss << indent(env.indent + 1) << "GMPI_REFCOUNT;\n";

		// end class
		env.oss << "};\n";
	}
};
struct printer_interface_cpp_classic : public lang_cpp
{
	void accept(environment env)
	{
		const auto struct_name = env.parent->Attribute("name");
		const auto structName = formatedName(struct_name, EType::Interface);
		const auto base_name = env.parent->Attribute("base");

		env.oss << "// INTERFACE '" << structName << "'\n";
		env.oss << "class DECLSPEC_NOVTABLE" << " " << structName;

		if (base_name)
		{
			env.oss << " : public " << formatedName(base_name, EType::Interface);
		}

		env.oss << "\n" << "{\n";

		// methods
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const auto datatype = e->Name();
			
			if (strcmp("set", datatype) == 0)
				continue; // for now

			const auto name = e->Attribute("name");
			const auto comments = e->Attribute("comment");

			std::string returnType = "int"; //  interfaceReturnType;
			if (auto returnTypeS = e->Attribute("returnType"); returnTypeS)
			{
				returnType = returnTypeS; // primary return type is passed like an 'in' arg (from DAWs perspective).
			}
			if (comments)
			{
				env.oss << indent(env.indent + 1) << "// " << comments << "\n";
			}
			env.oss << indent(env.indent + 1) << "virtual " << makeArgType(returnType, EArgType::In) << " MP_STDCALL " << PascalCase(name) << "(";
			bool first = true;
			for (auto arg = e->FirstChildElement(); arg; arg = arg->NextSiblingElement())
			{
				if (!first)
				{
					env.oss << ", ";
				}

				EArgType argtype = EArgType::In;
				if (strcmp(arg->Name(), "out") == 0)
				{
					argtype = EArgType::Out;
				}
				else if (strcmp(arg->Name(), "mut") == 0)
				{
					argtype = EArgType::Mutable;
				}

				auto arg_name = arg->Attribute("name");
				const auto arg_type = arg->Attribute("type");
				const bool isOutParameter = strcmp(arg->Name(), "out") == 0;
				if(!arg_name)
				{
					arg_name = arg_type;
				}
				env.oss << makeArgType(arg_type, argtype) << ' ' << nativeVariableName(arg_name, isOutParameter);

				first = false;
			}

			env.oss << ") = 0;\n";

		}

		// print the GUID
		/*
			// GUID for IMpAudioPluginHost.
			// {87CCD426-71D7-414E-A9A6-5ADCA81C7420}
			static const MpGuid MP_AUDIO_PLUGIN_HOST = // !!! should this be MP_IID_AUDIO_PLUGIN_HOST ???!!! (and others)
			{ 0x87ccd426, 0x71d7, 0x414e, { 0xa9, 0xa6, 0x5a, 0xdc, 0xa8, 0x1c, 0x74, 0x20 } };
		*/
		{
			const auto g = env.parent->Attribute("guid");
			if (g)
			{
				env.oss
					<< "\n"
					<< indent(env.indent + 1) << "// {" << std::string(g) << "}\n"
					<< indent(env.indent + 1) << "inline static const Guid guid =\n"
					<< indent(env.indent + 1);

				print_guid(env, g);
			}
		}

		// end class
		env.oss << "};\n";
	}
};

/*
	<enum name="return code" comment="Most methods return an error code.">
		<e name="handled" value="1" comment="Success. In case of GUI - no further handing required."/>

*/
struct printer_enum_cpp : public lang_cpp
{
	void accept(environment env)
	{
		const auto struct_name = env.parent->Attribute("name");
		const auto structName = formatedName(struct_name, EType::Enum);

		std::string enum_class = "enum class " + structName + " : int32_t";
		indenter en(env, enum_class.c_str());

		aligner aln(env);

		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			std::string name(e->Attribute("name"));
			const auto val = e->Attribute("value");
			const auto com = e->Attribute("comment");

			// can't have a pure numberic enum name in C++
			if(isdigit(name[0]))
			{
				name = structName + name;
			}

			std::string line = indent(env.indent + 1) + PascalCase(name);
			if (val)
			{
				line += " = " + std::string(val);
			}
			line += ",";

			if (com)
			{
				line += " // " + std::string(com);
			}
			aln.lines.push_back(line);
		}
	}
};

struct printer_enum_cpp_classic : public lang_cpp
{
	void accept(environment env)
	{
		const char* namespace_ = "MP1_";

		const auto struct_name = namespace_ + ALL_CAPS(env.parent->Attribute("name"));
		const auto enum_prefix = namespace_ + ALL_CAPS(env.parent->Attribute("name")) + "_";

		std::string enum_class = "enum " + struct_name;
		indenter en(env, enum_class.c_str());

//aligner aln(env);
		bool first = true;
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const auto name = e->Attribute("name");
			const auto val = e->Attribute("value");
			const auto com = e->Attribute("comment");

			if (!first)
			{
				env.oss << "\n";
			}
			std::string line = indent(env.indent + 1);
			if(!first)
			{
				line += ",";
			}
			line += enum_prefix + ALL_CAPS(name);

			if (val)
			{
				line += " = " + std::string(val);
			}

			if (com)
			{
				line += " // " + std::string(com);
			}
			env.oss << line << "\n";
			first = false;
		}
	}
};

struct printer_enum_c : public lang_c
{
	void accept(environment env)
	{
		const auto struct_name = namespace_ + PascalCase(env.parent->Attribute("name"));
		const auto enum_prefix = namespace_ + ALL_CAPS(env.parent->Attribute("name")) + "_";

		std::string enum_class = "enum " + struct_name;
		indenter en(env, enum_class.c_str());

		aligner aln(env);
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const auto name = e->Attribute("name");
			const auto val = e->Attribute("value");
			const auto com = e->Attribute("comment");

			//std::string line = indent(env.indent + 1) + namespace_ + ALL_CAPS(name) + " = " + val + ",";
			std::string line = indent(env.indent + 1) + enum_prefix + ALL_CAPS(name);
			if (val)
			{
				line += " = " + std::string(val);
			}
			line += ",";

			if (com)
			{
				line += " // " + std::string(com);
			}
			aln.lines.push_back(line);
		}
	}
};


struct printer_interface_c : public lang_c
{
	void accept(environment env)
	{
		const auto struct_name = env.parent->Attribute("name");
		const auto base_name = env.parent->Attribute("base");

		const auto structName = formatedName(struct_name, EType::Interface);
		const auto methodStructName = structName + "Methods";

		/*
		struct IGMPI_Unknown {
			struct IGMPI_UnknownMethods* methods;
		};
		*/
		env.oss << "// INTERFACE '" << structName << "'\n";

		env.oss
			<< indent(env.indent + 0) << "typedef struct" << " " << structName << "{\n"
			<< indent(env.indent + 1) << "struct " << methodStructName << "* methods;\n"
			<< indent(env.indent + 0) << "} " << structName << ";\n\n";

		/*
		typedef struct GMPI_IUnknownMethods
		{
			int32_t (*queryInterface)(GMPI_IUnknown*, const GMPI_Guid* iid, void** returnInterface);
			int32_t (*addRef)(GMPI_IUnknown*);
			int32_t (*release)(GMPI_IUnknown*);
		} GMPI_IUnknownMethods;
		*/
		env.oss << "typedef struct" << " " << methodStructName;
		env.oss << "\n" << "{\n";

		if (base_name)
		{
			env.oss << indent(env.indent + 1) << "// Methods of " << nativeVariableName(base_name) << "\n";
			for (auto& method : IUnknownMethods)
			{
				env.oss << method;
			}

			env.oss << "\n";
		}

		// methods
		std::vector < std::string > methods;
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const auto datatype = e->Name();
			auto name_ptr = e->Attribute("name");
			const auto comments = e->Attribute("comment");

			// make methods less verbose. e.g. <out name="gradientstop collection" type="gradientstop collection"/>
			std::string name;
			if (name_ptr)
				name = name_ptr;
			else
				name = datatype;

			if (comments)
			{
				env.oss << indent(env.indent + 1) << "// " << comments << "\n";
			}

			// collect arguments
			std::vector< argument > args;
			for (auto arg = e->FirstChildElement(); arg; arg = arg->NextSiblingElement())
			{
				args.push_back(argument{ ptrToString(arg->Name()), ptrToString(arg->Attribute("name")), ptrToString(arg->Attribute("type")) });
			}

			// shorthand for a simple setter
			if (strcmp("set", datatype) == 0)
			{
				const auto arg_type = e->Attribute("type");
				auto arg_name = e->Attribute("name");
				if (!arg_name)
				{
					arg_name = arg_type;
				}
				name = "set " + std::string(arg_name);

				args.push_back(argument{ ptrToString("in"), ptrToString(arg_name), ptrToString(arg_type) });
			}

			std::string returnType = interfaceReturnType;
			if (auto returnTypeS = e->Attribute("returnType"); returnTypeS)
			{
				returnType = makeArgType(returnTypeS, EArgType::In); // primary return type is passed like an 'in' arg (from DAWs perspective).
			}

			std::string method = indent(env.indent + 1) + returnType + " (*" + nativeVariableName(name) + ")(";

//			env.oss << indent(env.indent + 1) << interfaceReturnType << " (*" << nativeVariableName(name) << ")(";
			bool first = false;

			// 'this' is always first arg
			// env.oss << structName << "*";
			method += structName + "*";

			for (auto& arg : args)
			{
				if (!first)
				{
					method += ", ";
				}

				EArgType argtype = EArgType::In;
				if (arg.type == "out")
				{
					argtype = EArgType::Out;
				}
				else if (arg.type == "mut")
				{
					argtype = EArgType::Mutable;
				}

				auto arg_name = arg.name;
				const bool isOutParameter = argtype == EArgType::Out;
				if (arg_name.empty())
				{
					arg_name = arg.datatype;
				}
				method += makeArgType(arg.datatype, argtype) + ' ' + nativeVariableName(arg_name, argtype == EArgType::Out);
				first = false;
			}

			method += ");\n";

			methods.push_back(method);
		}

		for (auto& method : methods)
		{
			env.oss << method;
		}

		if (strcmp(struct_name, "unknown") == 0)
		{
			IUnknownMethods = methods;
		}

		env.oss << "} " << methodStructName << ";\n\n";

		// GUID
		{
			const auto g = env.parent->Attribute("guid");
			if (g)
			{
				env.oss
					<<
					"// {" << std::string(g) << "}\n"
					"static const GMPI_Guid GMPI_IID_" << ALL_CAPS(struct_name) << " =\n";

				print_guid(env, g);
			}
		}

	}
};

struct printer_api_cpp : public lang_cpp
{
	void accept(environment env)
	{
		include_guard ig(env);
		env.oss << copyright << "\n";

		if (strcmp(env.parent->Name(), "common_api") == 0)
		{
			env.oss << "#include <cstdint>\n#include <cassert>\n#include <vector>\n#include <string>";
		}
		else
		{
			env.oss << "#include <map>\n#include \"Common.h\"";
		}

		const bool isDrawingapi = strcmp(env.parent->Name(), "drawing_api") == 0;

		const char* namespace_name = isDrawingapi ? "namespace gmpi_drawing" : "namespace gmpi";

		env.oss << "\n\n" << platform_specific_prefix;

		{
			indenter ns(env, namespace_name);

			env.oss << "\n";

			bool first = true;
			for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
			{
				if (!first)
				{
					env.oss << "\n";
				}

				const auto etype = typeFromString(e->Name());

				const auto name = std::string(e->Attribute("name") ? e->Attribute("name") : "MISSING-NAME");
				typeNames.push_back({ name, etype });

				switch (etype)
				{
				case EType::Struct:
				{
					printer_struct_cpp p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
				case EType::Interface:
				{
					printer_interface_cpp p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
				case EType::Flags:
				case EType::Enum:
				{
					printer_enum_cpp p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
				};

				first = false;
			}
		}

		env.oss << "\n\n" << platform_specific_suffix;
	}
};

struct printer_api_wrap_cpp : public lang_cpp
{
	void accept(environment env)
	{
		include_guard ig(env);
		env.oss << copyright << "\n";

		if (strcmp(env.parent->Name(), "common_api") == 0)
		{
			env.oss << "#include <cstdint>\n#include <cassert>\n#include <vector>\n#include <string>";
		}
		else
		{
			env.oss << "#include <map>\n#include \"Common.h\"";
		}

		const bool isDrawingapi = strcmp(env.parent->Name(), "drawing_api") == 0;

		const char* namespace_name = isDrawingapi ? "namespace gmpi_drawing" : "namespace gmpi";

		env.oss << "\n\n" << platform_specific_prefix;

		{
			indenter ns(env, namespace_name);

			env.oss << "\n";

			bool first = true;
			for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
			{
				if (!first)
				{
					env.oss << "\n";
				}

				const auto etype = typeFromString(e->Name());

				const auto name = std::string(e->Attribute("name") ? e->Attribute("name") : "MISSING-NAME");
				typeNames.push_back({ name, etype });

				switch (etype)
				{
/* any real point? (just use structs, mayby with defaults)
									case EType::Struct:
				{
					printer_struct_cpp p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
			*/
				case EType::Interface:
				{
					printer_interface_wrapper_cpp p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
/*
// enums no longer require wrapping
		case EType::Flags:
				case EType::Enum:
				{
					printer_enum_cpp p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
*/
				};

				first = false;
			}
		}

		env.oss << "\n\n" << platform_specific_suffix;
	}
};

struct printer_platform_cpp : public lang_cpp
{
	void accept(environment env)
	{
		include_guard ig(env);
		env.oss << copyright << "\n";

		if (strcmp(env.parent->Name(), "common_api") == 0)
		{
			env.oss << "#include <cstdint>\n#include <cassert>\n#include <vector>\n#include <string>";
		}
		else
		{
			env.oss << "#include <map>\n#include \"Common.h\"";
		}

		const bool isDrawingapi = strcmp(env.parent->Name(), "drawing_api") == 0;

		const char* namespace_name = isDrawingapi ? "namespace directx" : "namespace gmpi";

		env.oss << "\n\n" << platform_specific_prefix;

		{
			indenter ns(env, "namespace gmpi");
			indenter ns2(env, namespace_name);

			env.oss << "\n";

			bool first = true;
			for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
			{
				if (!first)
				{
					env.oss << "\n";
				}

				const auto etype = typeFromString(e->Name());

				const auto name = std::string(e->Attribute("name") ? e->Attribute("name") : "MISSING-NAME");
				typeNames.push_back({ name, etype });

				switch (etype)
				{
				case EType::Interface:
				{
					printer_interface_platform_cpp p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
				};

				first = false;
			}
		}

		env.oss << "\n\n" << platform_specific_suffix;
	}
};

struct printer_api_cpp_classic : public lang_cpp
{
	void accept(environment env)
	{
		include_guard ig(env);
		env.oss << copyright << "\n";

		if (strcmp(env.parent->Name(), "common_api") == 0)
		{
			env.oss << "#include <cstdint>\n#include <cassert>\n#include <vector>\n#include <string>";
		}
		else
		{
			env.oss << "#include <map>\n#include \"Common.h\"";
		}

		const bool isDrawingapi = strcmp(env.parent->Name(), "drawing_api") == 0;

		const char* namespace_name = isDrawingapi ? "namespace gmpi_drawing" : "namespace gmpi";

		env.oss << "\n\n" << platform_specific_prefix;

		{
			indenter ns(env, namespace_name);

			env.oss << "\n";

			bool first = true;
			for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
			{
				if (!first)
				{
					env.oss << "\n";
				}

				const auto etype = typeFromString(e->Name());

				const auto name = std::string(e->Attribute("name") ? e->Attribute("name") : "MISSING-NAME");
				typeNames.push_back({ name, etype });

				switch (etype)
				{
				case EType::Struct:
				{
					printer_struct_cpp p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
				case EType::Interface:
				{
					printer_interface_cpp_classic p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
				case EType::Flags:
				case EType::Enum:
				{
					printer_enum_cpp_classic p;
					p.accept({ env.oss, e, env.indent });
				}
				break;
				};

				first = false;
			}
		}

		env.oss << "\n\n" << platform_specific_suffix;
	}
};

struct printer_api_c : public lang_c
{
	void accept(environment env)
	{
		namespace_ = "GMPI_";

		include_guard ig(env);

		if (strcmp(env.parent->Name(), "common_api") == 0)
		{
			env.oss << "#include <stdint.h>\n#include <stdbool.h>\n\n";
		}
		else
		{
			env.oss << "#include \"gmpi.h\"\n\n";
		}

		bool first = true;
		for (auto e = env.parent->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			if (!first)
			{
				env.oss << "\n";
			}

			const auto etype = typeFromString(e->Name());
			const auto name = std::string(e->Attribute("name") ? e->Attribute("name") : "MISSING-NAME");

			typeNames.push_back({ name, etype } );

			switch (etype)
			{
			case EType::Struct:
			{
				printer_struct_c p;
				p.accept({ env.oss, e, env.indent });
			}
			break;
			case EType::Interface:
			{
				printer_interface_c p;
				p.accept({ env.oss, e, env.indent });
			}
			break;
			case EType::Flags:
			case EType::Enum:
			{
				printer_enum_c p;
				p.accept({ env.oss, e, env.indent });
			}
			break;
			};

			first = false;
		}
	}
};

// Works only if user has permissions.
bool CreateFolderRecursive(std::wstring folderPath)
{
	std::vector<std::wstring> paths;

	while (folderPath.size() > 2)  // C:
	{
		paths.push_back(folderPath);

		auto p = folderPath.find_last_of(L"\\/");

		if (p != std::string::npos)
		{
			folderPath = folderPath.substr(0, p);
		}
	}

	for (auto it = paths.rbegin(); it != paths.rend(); ++it)
	{
#ifdef _WIN32
		// Create folder if not already.
		const int r = _wmkdir((*it).c_str());
		if (r)
		{
			if (GetLastError() != ERROR_ALREADY_EXISTS)
				return false;
		}
#else
		mkdir(WStringToUtf8(*it).c_str(), 0775);
#endif
	}

	return true;
}
int main()
{
	tinyxml2::XMLDocument doc;
#if 1
	doc.LoadFile("C:\\SE\\SE15\\SE_DSP_CORE\\modules\\se_sdk_gen\\GMPI.xml");
#else
	doc.LoadFile("C:\\SE\\SE15\\SE_DSP_CORE\\modules\\se_sdk_gen\\GMPI_UI.xml");
#endif

	CreateFolderRecursive(L"C:\\SE\\GMPI.generated\\Core");
	CreateFolderRecursive(L"C:\\SE\\GMPI.generated\\Projections\\plain_c\\Core\\");

	for (auto api = doc.FirstChildElement("apis")->FirstChildElement(); api; api = api->NextSiblingElement())
	{
		const std::string api_name(api->Name());

		// prints the API headers
		printer_api_cpp printer;
		std::ostringstream oss_cpp;
		printer.accept({ oss_cpp, api, 0 });

		// print the interface friendly wrappers. e.g. 'Brush' (wrapping IBrush)
		std::ostringstream oss_wrap_cpp;
		{
			printer_api_wrap_cpp printer;
			printer.accept({ oss_wrap_cpp, api, 0 });
		}

		// print the host-side implementation skeleton. wraps the interfaces again, but around the native functionality and types.
		std::ostringstream oss_platform_cpp;
		{
			printer_platform_cpp printer;
			printer.accept({ oss_platform_cpp, api, 0 });
		}


		// attempts to output the origninal GmpiDrawingAPI.h, to verify the accuracy of the XML spec.
		//printer_api_cpp_classic printer_cc;
		//std::ostringstream oss_cpp_clasic;
		//printer_cc.accept({ oss_cpp_clasic, api, 0 });

		printer_api_c printer_c;
		std::ostringstream oss_c;
		printer_c.accept({ oss_c, api, 0 });

		if (api_name == "common_api")
		{
			{
				std::ofstream file("C:\\SE\\GMPI.generated\\Core\\GmpiApiCommon.h");
				file << oss_cpp.str();
			}

			{
				std::ofstream file("C:\\SE\\GMPI.generated\\Projections\\plain_c\\Core\\gmpi.h");
				file << oss_c.str();
			}
		}
		else if (api_name == "audio_api")
		{
			{
				std::ofstream file("C:\\SE\\GMPI.generated\\Core\\GmpiApiAudio.h");
				file << oss_cpp.str();
			}
			{
				std::ofstream file("C:\\SE\\GMPI.generated\\Projections\\plain_c\\Core\\gmpi_audio.h");
				file << oss_c.str();
			}
		}
		else if (api_name == "drawing_api")
		{
			{
				std::ofstream file("C:\\SE\\GMPI.generated\\Core\\GmpiApiDrawing.h");
				file << oss_cpp.str();
			}
			{
				std::ofstream file("C:\\SE\\GMPI.generated\\Drawing.h");
				file << oss_wrap_cpp.str();
			}
			{
				std::ofstream file("C:\\SE\\GMPI.generated\\Projections\\plain_c\\Core\\gmpi_drawing.h");
				file << oss_c.str();
			}
			{
				std::ofstream file("C:\\SE\\GMPI.generated\\DirectXGfx.h");
				file << oss_platform_cpp.str();
			}
#if 0
			{
				std::ofstream file("C:\\SE\\GMPI.generated\\Core\\Drawing_API.h");
				file << oss_cpp_clasic.str();
			}
#endif
		}
	}

#if 0
	// Common header - cpp
	auto apis = doc.FirstChildElement("apis");
	{
		printer_api_cpp printer;
		std::ostringstream oss;
		printer.accept({ oss, apis->FirstChildElement("common_api"), 0 });

		std::ofstream file("C:\\SE\\GMPI.generated\\Core\\GmpiApiCommon.h");
		file << oss.str();
	}

	// Audio Plugin header - cpp
	{
		printer_api_cpp printer;
		std::ostringstream oss;
		printer.accept({ oss, apis->FirstChildElement("audio_api"), 0 });

		std::ofstream file("C:\\SE\\GMPI.generated\\Core\\GmpiApiAudio.h");
		file << oss.str();
	}

	// Common header - C
	{
		printer_api_c printer_c;
		std::ostringstream oss;
		printer_c.accept({ oss, apis->FirstChildElement("common_api"), 0 });

		std::ofstream file("C:\\SE\\GMPI.generated\\Projections\\plain_c\\Core\\gmpi.h");
		file << oss.str();
	}
	// Audio Plugin header - C
	{
		printer_api_c printer_c;
		std::ostringstream oss;
		printer_c.accept({ oss, apis->FirstChildElement("audio_api"), 0 });

		std::ofstream file("C:\\SE\\GMPI.generated\\Projections\\plain_c\\Core\\gmpi_audio.h");

		file << oss.str();
	}
#endif

//	system("pause");
}
