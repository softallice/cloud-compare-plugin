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
#include "Drawings.h"
#include "Planes.h"

// CCPluginAPI
#include <ccCommandLineInterface.h>

// qCC_db
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccHObject.h>

// Qt
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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
		QString              planPath;
		QString              elevPath;
		bool                 doPlanes = false;

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
			else if (ccCommandLineInterface::IsCommand(arg, "PLANES"))
			{
				cmd.arguments().pop_front();
				doPlanes = true;
			}
			else if (ccCommandLineInterface::IsCommand(arg, "PLAN"))
			{
				cmd.arguments().pop_front();
				if (cmd.arguments().empty())
					return cmd.error(QStringLiteral("Missing path after -PLAN"));
				planPath = cmd.arguments().takeFirst();
			}
			else if (ccCommandLineInterface::IsCommand(arg, "ELEV"))
			{
				cmd.arguments().pop_front();
				if (cmd.arguments().empty())
					return cmd.error(QStringLiteral("Missing path after -ELEV"));
				elevPath = cmd.arguments().takeFirst();
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

			// --- L2: plane / storey analysis (on demand) ------------------------
			const bool needPlanes = doPlanes || !planPath.isEmpty() || !elevPath.isEmpty();
			Planes::Model planesModel;
			if (needPlanes)
			{
				planesModel = Planes::detect(pc);
				cmd.print(QStringLiteral("[BUILDING_DIMS] planes=%1 storeys=%2 storeyH=%3")
				              .arg(planesModel.planes.size())
				              .arg(planesModel.storeyCount)
				              .arg(planesModel.storeyHeight, 0, 'f', 2));
			}

			if (!jsonPath.isEmpty())
			{
				// Start from the dimensions doc, optionally merge the plane analysis.
				QJsonObject root =
				    QJsonDocument::fromJson(BuildingDims::toJson(res, desc.basename)).object();
				if (needPlanes)
					root[QStringLiteral("planes_analysis")] = Planes::toJson(planesModel, unit);

				QFile jf(jsonPath);
				if (jf.open(QIODevice::WriteOnly | QIODevice::Text))
				{
					jf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
					jf.close();
					cmd.print(QStringLiteral("[BUILDING_DIMS] JSON -> %1").arg(jsonPath));
				}
				else
				{
					cmd.warning(QStringLiteral("[BUILDING_DIMS] failed to write JSON: %1").arg(jsonPath));
				}
			}

			if (!planPath.isEmpty())
			{
				const QString err = Drawings::writePlan(res, planesModel, planPath);
				if (!err.isEmpty())
					cmd.warning(QStringLiteral("[BUILDING_DIMS] %1").arg(err));
				else
					cmd.print(QStringLiteral("[BUILDING_DIMS] PLAN -> %1").arg(planPath));
			}

			if (!elevPath.isEmpty())
			{
				const QString err = Drawings::writeElevations(planesModel, unit, elevPath);
				if (!err.isEmpty())
					cmd.warning(QStringLiteral("[BUILDING_DIMS] %1").arg(err));
				else
					cmd.print(QStringLiteral("[BUILDING_DIMS] ELEV -> %1").arg(elevPath));
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
