#include <CppPkg/Main.hpp>

#include <CppPkg/Help.hpp>
#include <CppPkg/Errors.hpp>
#include <CppPkg/Package.hpp>
#include <CppPkg/Generators/Premake5.hpp>
#include <CppPkg/Readers/General.hpp>
#include <CppPkg/Readers/JSONReader.hpp>

#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs 	= std::filesystem;

// Forward declarations
void handleArgs(ProgramArgs const & args_);
void displayHelp(ProgramArgs const& args_, bool full_);
void buildPackage(ProgramArgs const& args_);
void initPackage();
Package fromJSON(std::string const& packageContent_);



///////////////////////////////////////////////////
int main(int argc, char *argv[])
{
	ProgramArgs args{ argv, argv + argc };

	try {
		handleArgs(args);
	}
	catch(std::exception & exc)
	{
		std::cerr 	<< "An error occurred. Details:\n" << exc.what();

		return 1;
	}
	catch(...)
	{
		std::cerr 	<< "An error occurred. No details available."
					<< "\nPlease refer to https://github.com/PoetaKodu/cpp-pkg/issues"
					<< std::endl;

		return 1;
	}
}

///////////////////////////////////////////////////
void handleArgs(ProgramArgs const& args_)
{
	if (args_.size() < 2)
	{
		displayHelp(args_, true);
	}
	else
	{
		auto action = args_[1];

		if (action == "help")
		{
			displayHelp(args_, false);
		}
		else if (action == "init")
		{
			initPackage();
		}
		else if (action == "build")
		{
			buildPackage(args_);
		}
		else
		{
			auto programName = fs::u8path(args_[0]).stem();

			std::cerr 	<< "Error:\tunsupported action \"" << action
						<< "\".\n\tUse \"" << programName << " help\" to list available actions."
						<< std::endl;
		}
	}
}

///////////////////////////////////////////////////
void displayHelp(ProgramArgs const& args_, bool abbrev_)
{
	auto programName = fs::u8path(args_[0]).stem();

	// Introduction:
	std::cout 	<< "A C++ package manager.\n\n"
			 	<< "USAGE: " << programName.string() << " [action] <params>\n\n";

	// 
	if (abbrev_)
	{
		std::cout 	<< "Use \"" << programName.string() << " help\" for more information"
					<< std::endl;
	}
	else
	{
		// Display actions
		std::cout << "ACTIONS\n";
					
		for (auto action : help::actions)
		{
			std::cout << "\t" << action.first << "\t\t" << action.second << "\n";
		}
		std::cout << std::endl;
	}
}

///////////////////////////////////////////////////
void initPackage()
{
	auto cwd = fs::current_path();


	std::cout << "Initializing package \"" << cwd.stem().string() << "\"" << std::endl;
	std::cout << "Do you want to create \"cpackage.json\" file (Y/N): ";

	std::string response;
	std::getline(std::cin, response);

	if (response[0] != 'y' && response[0] != 'Y')
	{
		std::cout << "Action aborted." << std::endl;
		return;
	}

	std::ofstream("cpackage.json") <<
R"PKG({
	"$schema": "https://raw.githubusercontent.com/PoetaKodu/cpp-pkg/main/res/cpackage.schema.json",
	"name": "MyWorkspace",
	"projects": [
		{
			"name": "MyProject",
			"type": "app",
			"language": "C++17",
			"files": "src/*.cpp"
		}
	]
})PKG";

	std::cout 	<< "\"cpackage.json\" has been created.\n"
				<< "Happy development!" << std::endl;
}


///////////////////////////////////////////////////
void buildPackage(ProgramArgs const& args_)
{
	constexpr std::string_view PackageJSON 	= "cpackage.json";
	constexpr std::string_view PackageLUA 	= "cpackage.lua";

	auto cwd = fs::current_path();

	enum class PackageFileSource
	{
		JSON,
		LuaScript
	};
	PackageFileSource pkgSrcFile;
	

	// Detect package file
	if (fs::exists(cwd / PackageLUA)) // LuaScript has higher priority
	{
		pkgSrcFile = PackageFileSource::LuaScript;
	}
	else if (fs::exists(cwd / PackageJSON))
	{
		pkgSrcFile = PackageFileSource::JSON;
	}
	else
		throw std::exception(errors::NoPackageSourceFile.data());
	

	Package pkg;

	// Decide what to do:
	switch(pkgSrcFile)
	{
	case PackageFileSource::JSON:
	{
		std::cout << "Loading \"" << PackageJSON << "\" file\n";\

		pkg = reader::fromJSON(reader::readFileContents(PackageJSON));
		break;
	}
	case PackageFileSource::LuaScript:
	{
		std::cout << "Loading \"" << PackageLUA << "\" file\n";

		// TODO: implement this.
		std::cout << "This function is not implemented yet." << std::endl;
		break;
	}
	}

	gen::Premake5 g;
	g.generate(pkg);
}





