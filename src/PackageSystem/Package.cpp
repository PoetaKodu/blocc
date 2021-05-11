#include PACC_PCH

#include <Pacc/PackageSystem/Package.hpp>
#include <Pacc/App/Errors.hpp>
#include <Pacc/System/Environment.hpp>
#include <Pacc/Readers/General.hpp>
#include <Pacc/System/Filesystem.hpp>
#include <Pacc/Readers/JsonReader.hpp>
#include <Pacc/Generation/BuildQueueBuilder.hpp>


///////////////////////////////////////////////////
// Private functions (forward declaration)
///////////////////////////////////////////////////

template <json::value_t type>
json const* expect(json const& j);

template <json::value_t type>
json const* expectSub(json const& j, std::string_view subfieldName);

template <json::value_t type>
json const& require(json const& j);

template <json::value_t type>
json const& requireSub(json const& j, std::string_view subfieldName);

json const* 	selfOrSubfieldOpt(json const& self, std::string_view fieldName = "");
json const& 	selfOrSubfieldReq(json const& self, std::string_view fieldName = "");
json const* 	selfOrSubfield(json const& self, std::string_view fieldName, bool required = false);

void 			readDependencyAccess(json const& deps_, std::vector<Dependency> &target_);
VecOfStr 		loadVecOfStrField(json const& j, std::string_view fieldName, bool direct = false, bool required = false);
VecOfStrAcc 	loadVecOfStrAccField(json const& j, std::string_view fieldName);


///////////////////////////////////////////////////
// Public functions
///////////////////////////////////////////////////


///////////////////////////////////////////////////
void TargetBase::inheritConfigurationFrom(Package const& fromPkg_, Project const& fromProject_, AccessType mode_)
{
	computeConfiguration( *this, fromPkg_, fromProject_, mode_ );

	// Inherit all premake filters:
	for(auto it : fromProject_.premakeFilters)
	{	
		// Ensure configuration exists:
		if (premakeFilters.find(it.first) == premakeFilters.end())
			premakeFilters[it.first] = {};

		// Merge configuration:
		computeConfiguration( premakeFilters.at(it.first), fromPkg_, fromProject_, it.second, mode_ );
	}
}

///////////////////////////////////////////////////
Package Package::load(fs::path dir_)
{
	if (dir_.empty()) {
		dir_ = fs::current_path();
	}

	enum class PackageFileSource
	{
		JSON,
		LuaScript
	};

	PackageFileSource pkgSrcFile;
	
	Package pkg;

	// Detect package file
	if (fs::exists(dir_ / PackageLUA)) // LuaScript has higher priority
	{
		pkgSrcFile = PackageFileSource::LuaScript;
		pkg.root = dir_ / PackageLUA;
	}
	else if (fs::exists(dir_ / PackageJSON))
	{
		pkgSrcFile = PackageFileSource::JSON;
	}
	else
		throw PaccException(errors::NoPackageSourceFile[0])
			.withHelp(errors::NoPackageSourceFile[1]);
	

	// Decide what to do:
	switch(pkgSrcFile)
	{
	case PackageFileSource::JSON:
	{
		// std::cout << "Loading \"" << PackageJSON << "\" file\n";\

		pkg = Package::loadFromJSON(readFileContents(dir_ / PackageJSON));
		pkg.root = dir_ / PackageJSON;
		break;
	}
	case PackageFileSource::LuaScript:
	{
		// std::cout << "Loading \"" << PackageLUA << "\" file\n";


		// TODO: implement this.
		std::cout << "This function is not implemented yet." << std::endl;
		pkg.root = dir_ / PackageLUA;
		break;
	}
	}
	return pkg;
}

/////////////////////////////////////////////////
Package Package::loadByName(std::string_view name_)
{
	const std::vector<fs::path> candidates = {
			fs::current_path() 					/ "pacc_packages",
			env::getPaccDataStorageFolder() 	/ "packages"
		};

	// Get first matching candidate:
	for(auto const& c : candidates)
	{
		auto pkgFolder = c / name_;
		try {
			return Package::load(pkgFolder);
		}
		catch(...)
		{
			// Could not load, ignore
		}
	}

	// Found none.
	throw std::runtime_error(fmt::format("Could not find package \"{}\" (TODO: help here).", name_));
}


///////////////////////////////////////////////////
Project const* Package::findProject(std::string_view name_) const
{
	auto it = std::find_if(projects.begin(), projects.end(),
		[&](auto const& e) {
			return e.name == name_;
		});

	if (it != projects.end())
		return &(*it);

	return nullptr;
}

///////////////////////////////////////////////////
Project const& Package::requireProject(std::string_view name_) const
{
	Project const *proj = this->findProject(name_);
	if (!proj)
		throw PaccException("Project \"{}\" does not exist in package \"{}\"", name_, name);

	return *proj;
}


///////////////////////////////////////////////////
fs::path Package::predictOutputFolder(Project const& project_) const
{
	// TODO: make it configurable:
	return this->root.parent_path() / "bin/%{cfg.platform}/%{cfg.buildcfg}";
}

///////////////////////////////////////////////////
fs::path Package::predictRealOutputFolder(Project const& project_, BuildSettings settings_) const
{
	std::string folder =  fmt::format("bin/{}/{}",
			settings_.platformName,
			settings_.configName
		);

	return this->root.parent_path() / folder;
}

///////////////////////////////////////////////////
fs::path Package::resolvePath( fs::path const& path_) const
{
	if (path_.is_relative())
		return fsx::fwd(root.parent_path() / path_).string();
	else 
		return path_;
}

///////////////////////////////////////////////////
void loadConfigurationFromJSON(Configuration& conf_, json const& root_)
{
	using json_vt = json::value_t;
	
	conf_.files		 			= loadVecOfStrField(root_, "files");
	conf_.defines.self	 		= loadVecOfStrAccField(root_, "defines");
	conf_.includeFolders.self	= loadVecOfStrAccField(root_, "includeFolders");
	conf_.linkerFolders.self	= loadVecOfStrAccField(root_, "linkerFolders");

	// Load dependencies:		
	auto depsIt = root_.find("dependencies");
	if (depsIt != root_.end())
	{
		auto& deps = depsIt.value();
		auto& projSelfDeps = conf_.dependencies.self;
		if (deps.type() == json_vt::array)
		{
			readDependencyAccess(*depsIt, projSelfDeps.private_);
		}
		else if (deps.type() == json_vt::object)
		{
			if (deps.contains("public")) 		readDependencyAccess(deps["public"], projSelfDeps.public_);
			if (deps.contains("private")) 		readDependencyAccess(deps["private"], projSelfDeps.private_);
			if (deps.contains("interface")) 	readDependencyAccess(deps["interface"], projSelfDeps.interface_);
		}
		else
			throw std::runtime_error("Invalid type of \"dependencies\" field (must be an array or an object)");
	}
}

///////////////////////////////////////////////////
Package Package::loadFromJSON(std::string const& packageContent_)
{
	using json_vt = json::value_t;

	Package result;

	// Parse and make conformant:
	json j;
	PackageJsonReader view{ j };

	j = json::parse(packageContent_);
	view.makeConformant();
	
	// std::ofstream("package.dump.json") << j.dump(1, '\t');

	// Load JSON:
	result.name = j["name"].get<std::string>();

	auto projects = j.find("projects");

	result.projects.reserve(projects->size());

	
	// Read projects:
	for(auto it : projects->items())
	{
		auto& jsonProject = it.value();

		Project project;

		project.name 					= jsonProject["name"].get<std::string>();
		project.type 					= jsonProject["type"].get<std::string>();

		if (auto it = jsonProject.find("pch"); it != jsonProject.end())
		{
			PrecompiledHeader pch;
			// TODO: add validation
			pch.header		= jsonProject["pch"]["header"];
			pch.source		= jsonProject["pch"]["source"];
			pch.definition 	= jsonProject["pch"]["definition"];
			project.pch = std::move(pch);
		}

		// TODO: type and value validation
		if (auto it = jsonProject.find("language"); it != jsonProject.end())
			project.language = it->get<std::string>();

		loadConfigurationFromJSON(project, jsonProject);

		json const* filters = expectSub<json_vt::object>(jsonProject, "filters");
		if (filters)
		{
			for(auto filterIt : filters->items())
			{
				auto const& val = filterIt.value();
				if (val.type() == json_vt::object)
				{
					// Create and reference the configuration:
					Configuration& cfg = project.premakeFilters[filterIt.key()];
					loadConfigurationFromJSON(cfg, val);
				}
			}
		}
		
		result.projects.push_back(std::move(project));
	}

	return result;
}

/////////////////////////////////////////////////
std::size_t getNumElements(VecOfStr const& v)
{
	return v.size();
}

/////////////////////////////////////////////////
std::size_t getNumElements(VecOfStrAcc const& v)
{
	return v.public_.size() + v.private_.size() + v.interface_.size();
}


/////////////////////////////////////////////////
void computeConfiguration(Configuration& into_, Package const& fromPkg_, Project const& fromProject_, Configuration const& from_, AccessType mode_)
{
	auto resolvePath = [&](auto const& pathLikeElem)
		{
			return fromPkg_.resolvePath(fs::u8path(pathLikeElem)).string();
		};

	mergeAccesses(into_.defines, 			from_.defines, 		 		mode_);
	mergeAccesses(into_.includeFolders, 	from_.includeFolders,  		mode_, resolvePath);
	mergeAccesses(into_.linkerFolders, 		from_.linkerFolders,  		mode_, resolvePath);
	mergeAccesses(into_.linkedLibraries, 	from_.linkedLibraries, 		mode_);

	// TODO: case, enums
	if (fromProject_.type == "static lib" || fromProject_.type == "shared lib")
	{
		// Add dependency output folder:
		{
			auto& target = targetByAccessType(into_.linkerFolders.computed, mode_);
			target.push_back(fsx::fwd(fromPkg_.predictOutputFolder(fromProject_)).string());
		}
		
		// Add dependency file to linker:
		{
			auto& target = targetByAccessType(into_.linkedLibraries.computed, mode_);
			target.push_back(fromProject_.name);
		}
	}
}

///////////////////////////////////////////////////
// Private functions
///////////////////////////////////////////////////

///////////////////////////////////////////////////
void readDependencyAccess(json const& deps_, std::vector<Dependency> &target_)
{
	using json_vt = json::value_t;

	if (deps_.type() != json_vt::array)
		throw std::runtime_error("invalid type of dependencies subfield - array required");

	target_.reserve(deps_.size());

	for(auto item : deps_.items())
	{
		if (json const* rawDep = expect<json_vt::string>(item.value()))
		{
			target_.push_back(
					Dependency::raw( std::move( rawDep->get<std::string>() ) )
				);
		}
		else if (json const* pkgDep = expect<json_vt::object>(item.value()))
		{
			// Required fields:
			json const& name 		= requireSub<json_vt::string>(*pkgDep, "name");
			json const& projects 	= requireSub<json_vt::array>(*pkgDep, "projects");
			// Optional fields:
			json const* version 	= expectSub<json_vt::string>(*pkgDep, "version");

			// Configure dependency:
			PackageDependency pd;

			// Required:
			pd.packageName = name;
			
			pd.projects.reserve(projects.size());
			for(auto proj : projects.items())
			{
				json const& projName = require<json_vt::string>(proj.value());

				pd.projects.push_back(projName.get<std::string>());
			}

			// Optional
			if (version) {
				pd.version = version->get<std::string>();
			}

			target_.push_back(
					Dependency::package( std::move(pd) )
				);
		}
		else
			throw std::runtime_error("Invalid dependency type");
	}

	
}


///////////////////////////////////////////////////
json const* selfOrSubfieldOpt(json const &self, std::string_view fieldName)
{
	if (fieldName == "")
		return &self;
	else
	{
		if (auto it = self.find(fieldName); it != self.end())
			return &it.value();
	}

	return nullptr;
}

///////////////////////////////////////////////////
json const& selfOrSubfieldReq(json const &self, std::string_view fieldName)
{
	json const* v = selfOrSubfieldOpt(self, fieldName);
	if (!v)
		throw std::runtime_error(fmt::format("field {0} not found", fieldName));
	else
		return *v;
}

///////////////////////////////////////////////////
json const* selfOrSubfield(json const &self, std::string_view fieldName, bool required)
{
	if (required)
		return &selfOrSubfieldReq(self, fieldName);
	else
		return selfOrSubfieldOpt(self, fieldName);
}

///////////////////////////////////////////////////
VecOfStr loadVecOfStrField(json const &j, std::string_view fieldName, bool direct, bool required)
{
	using JV = JsonView;

	VecOfStr result;
	std::string const elemName = std::string(fieldName) + " element";

	// Either subfield or the `j` itself (direct => `j` is an array)
	json const* val = selfOrSubfield(j, direct ? "" : fieldName, required);

	// Can be null if `required` == false
	if (!val)
		return result;

	if (val->type() == json::value_t::string)
	{
		result.push_back(*val);
	}
	else
	{
		JV(*val).requireType(fieldName, json::value_t::array);
		
		// Read the array:
		result.reserve(val->size());

		for(auto elem : val->items())
		{
			JV{elem.value()}.requireType(elemName, json::value_t::string);
			result.push_back(elem.value());
		}
	}
	return result;
}

///////////////////////////////////////////////////
VecOfStrAcc loadVecOfStrAccField(json const &j, std::string_view fieldName)
{
	VecOfStrAcc result;
	if (auto it = j.find(fieldName); it != j.end())
	{
		if (it.value().type() == json::value_t::array)
			result.private_ = loadVecOfStrField(*it, fieldName, true);
		else
		{
			result.private_ 	= loadVecOfStrField(*it, "private");
			result.public_ 		= loadVecOfStrField(*it, "public");
			result.interface_ 	= loadVecOfStrField(*it, "interface");
		}
	}	
	return result;
}

///////////////////////////////////////////////////
template <json::value_t type>
json const* expect(json const &j)
{
	if (j.type() == type)
		return &j;
	else
		return nullptr;
}

///////////////////////////////////////////////////
template <json::value_t type>
json const* expectSub(json const &j, std::string_view subfieldName)
{
	auto it = j.find(subfieldName);
	if (it != j.end() && it->type() == type)
	{
		return (&(*it));
	}

	return nullptr;
}

///////////////////////////////////////////////////
template <json::value_t type>
json const& require(json const &j)
{
	if (j.type() == type)
		return j;
	else
		throw std::runtime_error("invalid type");
}

///////////////////////////////////////////////////
template <json::value_t type>
json const& requireSub(json const &j, std::string_view subfieldName)
{
	auto it = j.find(subfieldName);
	if (it != j.end() && it->type() == type)
	{
		return (*it);
	}

	throw std::runtime_error("invalid subfield type");
}