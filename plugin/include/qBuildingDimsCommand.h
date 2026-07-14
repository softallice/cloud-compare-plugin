// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                                                                        #
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//#  Command-line command:  -BUILDING_DIMS                                 #
//#                                                                        #
//##########################################################################

#pragma once

#include "BuildingDims.h"

// CCPluginAPI
#include <ccCommandLineInterface.h>

// qCC_db
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccHObject.h>

//! -BUILDING_DIMS [-METHOD OBB|AABB] [-UNIT m] [-JSON out.json] [-DXF out.dxf]
/** Operates on every cloud currently loaded on the command-line stack. **/
struct CommandBuildingDims : public ccCommandLineInterface::Command
{
	CommandBuildingDims()
	    : ccCommandLineInterface::Command(QStringLiteral("Building dimensions"),
	                                      QStringLiteral("BUILDING_DIMS"))
	{
	}

	bool process(ccCommandLineInterface& cmd) override
	{
		cmd.print(QStringLiteral("[BUILDING_DIMS] start"));

		BuildingDims::Method method = BuildingDims::Method::OBB;
		QString              unit   = QStringLiteral("m");
		QString              jsonPath;
		QString              dxfPath;

		// --- parse sub-options ---------------------------------------------------
		while (!cmd.arguments().empty())
		{
			const QString& arg = cmd.arguments().front();
			if (ccCommandLineInterface::IsCommand(arg, "METHOD"))
			{
				cmd.arguments().pop_front();
				if (cmd.arguments().empty())
					return cmd.error(QStringLiteral("Missing value after -METHOD"));
				const QString m = cmd.arguments().takeFirst().toUpper();
				if (m == QStringLiteral("AABB"))
					method = BuildingDims::Method::AABB;
				else if (m == QStringLiteral("OBB"))
					method = BuildingDims::Method::OBB;
				else
					return cmd.error(QStringLiteral("Unknown -METHOD value: %1").arg(m));
			}
			else if (ccCommandLineInterface::IsCommand(arg, "UNIT"))
			{
				cmd.arguments().pop_front();
				if (cmd.arguments().empty())
					return cmd.error(QStringLiteral("Missing value after -UNIT"));
				unit = cmd.arguments().takeFirst();
			}
			else if (ccCommandLineInterface::IsCommand(arg, "JSON"))
			{
				cmd.arguments().pop_front();
				if (cmd.arguments().empty())
					return cmd.error(QStringLiteral("Missing path after -JSON"));
				jsonPath = cmd.arguments().takeFirst();
			}
			else if (ccCommandLineInterface::IsCommand(arg, "DXF"))
			{
				cmd.arguments().pop_front();
				if (cmd.arguments().empty())
					return cmd.error(QStringLiteral("Missing path after -DXF"));
				dxfPath = cmd.arguments().takeFirst();
			}
			else
			{
				// Not one of our sub-options: hand control back to the parser.
				break;
			}
		}

		if (cmd.clouds().empty())
		{
			return cmd.error(QStringLiteral("[BUILDING_DIMS] No cloud loaded (use -O first)."));
		}

		for (CLCloudDesc& desc : cmd.clouds())
		{
			ccPointCloud* pc = desc.pc;
			if (!pc)
				continue;

			BuildingDims::Result res = BuildingDims::compute(pc, method, unit);
			if (!res.valid)
			{
				cmd.warning(QStringLiteral("[BUILDING_DIMS] %1: extraction failed").arg(desc.basename));
				continue;
			}

			cmd.print(QStringLiteral("[BUILDING_DIMS] %1: L=%2 W=%3 H=%4 %5")
			              .arg(desc.basename)
			              .arg(res.length, 0, 'f', 3)
			              .arg(res.width, 0, 'f', 3)
			              .arg(res.height, 0, 'f', 3)
			              .arg(unit));

			if (!jsonPath.isEmpty())
			{
				if (!BuildingDims::writeJson(res, jsonPath, desc.basename))
					cmd.warning(QStringLiteral("[BUILDING_DIMS] failed to write JSON: %1").arg(jsonPath));
				else
					cmd.print(QStringLiteral("[BUILDING_DIMS] JSON -> %1").arg(jsonPath));
			}

			if (!dxfPath.isEmpty())
			{
				ccPolyline* poly = BuildingDims::buildFootprintPolyline(res, pc);
				if (!poly)
				{
					cmd.warning(QStringLiteral("[BUILDING_DIMS] failed to build footprint"));
				}
				else
				{
					const QString err = BuildingDims::exportDxf(poly, dxfPath);
					if (!err.isEmpty())
						cmd.warning(QStringLiteral("[BUILDING_DIMS] %1").arg(err));
					else
						cmd.print(QStringLiteral("[BUILDING_DIMS] DXF -> %1").arg(dxfPath));
					delete poly;
				}
			}
		}

		return true;
	}
};
