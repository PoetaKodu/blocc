#pragma once

#include <Pacc/Toolchains/Toolchain.hpp>

struct GNUMakeToolchain : Toolchain
{
	virtual Type type() const { return GNUMake; }

	// TODO: Choose between "gmake" and "gmake2"
	virtual std::string premakeToolchainType() const { return "gmake2"; }

	virtual std::optional<int> run(Package const & pkg_, BuildSettings settings_ = {}, int verbosityLevel_ = 0) override;

	static std::vector<GNUMakeToolchain> detect();
};