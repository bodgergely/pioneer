#include "Ship.h"
#include "Frame.h"
#include "Pi.h"
#include "WorldView.h"
#include "Space.h"
#include "ModelCollMeshData.h"
#include "SpaceStation.h"
#include "Serializer.h"

static ObjParams params = {
	{ 0.5, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f },

	{	// pColor[3]
	{ { 1.0f, 0.0f, 1.0f }, { 0, 0, 0 }, { 0, 0, 0 }, 0 },
	{ { 0.8f, 0.6f, 0.5f }, { 0, 0, 0 }, { 0, 0, 0 }, 0 },
	{ { 0.5f, 0.5f, 0.5f }, { 0, 0, 0 }, { 0, 0, 0 }, 0 } },

	// pText[3][256]	
	{ "IR-L33T", "ME TOO" },
};

void Ship::Save()
{
	using namespace Serializer::Write;
	DynamicBody::Save();
	wr_int(Serializer::LookupBody(m_combatTarget));
	wr_int(Serializer::LookupBody(m_navTarget));
	wr_float(m_dockingTimer);
	wr_float(m_angThrusters[0]);
	wr_float(m_angThrusters[1]);
	wr_float(m_angThrusters[2]);
	for (int i=0; i<ShipType::THRUSTER_MAX; i++) wr_float(m_thrusters[i]);
	wr_float(m_wheelTransition);
	wr_float(m_wheelState);
	wr_float(m_launchLockTimeout);
	wr_bool(m_testLanded);
	wr_int((int)m_flightState);
	for (int i=0; i<ShipType::GUNMOUNT_MAX; i++) wr_int(m_gunState[i]);
	wr_int((int)m_shipType);
	wr_int(m_dockedWithPort);
	wr_int(Serializer::LookupBody(m_dockedWith));
	printf("XXXXXXX NOT SAVING SHIP EQUIPMENT YET!!!!!!!!!!!!!!\n");
}

void Ship::Load()
{
	using namespace Serializer::Read;
	DynamicBody::Load();
	// needs fixups
	m_combatTarget = (Body*)rd_int();
	m_navTarget = (Body*)rd_int();
	m_dockingTimer = rd_float();
	m_angThrusters[0] = rd_float();
	m_angThrusters[1] = rd_float();
	m_angThrusters[2] = rd_float();
	for (int i=0; i<ShipType::THRUSTER_MAX; i++) m_thrusters[i] = rd_float();
	m_wheelTransition = rd_float();
	m_wheelState = rd_float();
	m_launchLockTimeout = rd_float();
	m_testLanded = rd_bool();
	m_flightState = (FlightState) rd_int();
	for (int i=0; i<ShipType::GUNMOUNT_MAX; i++) {
		m_gunState[i] = rd_int();
		m_tempLaserGeom[i] = 0;
	}
	m_shipType = (ShipType::Type)rd_int();
	m_dockedWithPort = rd_int();
	m_dockedWith = (SpaceStation*)rd_int();
	/// XXXXXXXXXXXXXXX
	m_equipment = EquipSet(m_shipType);
	Init();
}

void Ship::Init()
{
	const ShipType &stype = GetShipType();
	SetModel(stype.sbreModel);
	SetMassDistributionFromCollMesh(GetModelSBRECollMesh(stype.sbreModel));
	GeomsSetBody(m_body);
	UpdateMass();
}

void Ship::PostLoadFixup()
{
	m_combatTarget = Serializer::LookupBody((size_t)m_combatTarget);
	m_navTarget = Serializer::LookupBody((size_t)m_navTarget);
	m_dockedWith = (SpaceStation*)Serializer::LookupBody((size_t)m_dockedWith);
}

Ship::Ship(ShipType::Type shipType): DynamicBody()
{
	m_flightState = FLYING;
	m_testLanded = false;
	m_launchLockTimeout = 0;
	m_wheelTransition = 0;
	m_wheelState = 0;
	m_dockedWith = 0;
	m_dockedWithPort = 0;
	m_dockingTimer = 0;
	m_navTarget = 0;
	m_combatTarget = 0;
	m_shipType = shipType;
	m_angThrusters[0] = m_angThrusters[1] = m_angThrusters[2] = 0;
	m_laserCollisionObj.owner = this;
	m_equipment = EquipSet(shipType);
	for (int i=0; i<ShipType::GUNMOUNT_MAX; i++) {
		m_tempLaserGeom[i] = 0;
		m_gunState[i] = 0;
	}
	memset(m_thrusters, 0, sizeof(m_thrusters));

	Init();	
}

void Ship::UpdateMass()
{
	CalcStats();
	dMassAdjust(&m_mass, m_stats.total_mass*1000);
	dBodySetMass(m_body, &m_mass);
}

bool Ship::OnCollision(Body *b, Uint32 flags)
{
	if (b->IsType(Object::PLANET)) {
		// geoms still enabled when landed
		if (m_flightState != FLYING) return false;
		else m_testLanded = true;
	}
	if (b->IsType(Object::MODELBODY)) {
		vector3d relVel;
		if (b->IsType(Object::DYNAMICBODY)) {
			relVel = static_cast<DynamicBody*>(b)->GetVelocity() - GetVelocity();
		} else {
			relVel = GetVelocity();
		}
		// crappy recalculation of all this...
		float v = relVel.Length();
		float kineticEnergy = m_stats.total_mass * v * v;
		printf("%f ek\n", kineticEnergy);
	}
	return true;
}

vector3d Ship::CalcRotDamping()
{
	// rotation damping.
	const dReal *_av = dBodyGetAngularVel(m_body);
	vector3d angVel(_av[0], _av[1], _av[2]);
	matrix4x4d rot;
	GetRotMatrix(rot);
	angVel = rot.InverseOf() * angVel;

	return angVel * 0.6;
}

void Ship::SetThrusterState(enum ShipType::Thruster t, float level)
{
	m_thrusters[t] = level;
}

void Ship::ClearThrusterState()
{
	SetAngThrusterState(0, 0.0f);
	SetAngThrusterState(1, 0.0f);
	SetAngThrusterState(2, 0.0f);

	if (m_launchLockTimeout == 0) {
		for (int i=0; i<ShipType::THRUSTER_MAX; i++) m_thrusters[i] = 0;
	}
}

// hyperspace range is:
// (200 * hyperspace_class^2) / total mass (in tonnes)

const shipstats_t *Ship::CalcStats()
{
	const ShipType &stype = GetShipType();
	m_stats.max_capacity = stype.capacity;
	m_stats.used_capacity = 0;

	for (int i=0; i<Equip::SLOT_MAX; i++) {
		for (int j=0; j<stype.equipSlotCapacity[i]; j++) {
			Equip::Type t = m_equipment.Get((Equip::Slot)i, j);
			if (t) m_stats.used_capacity += EquipType::types[t].mass;
		}
	}
	m_stats.free_capacity = m_stats.max_capacity - m_stats.used_capacity;
	m_stats.total_mass = m_stats.used_capacity + stype.hullMass;

	Equip::Type t = m_equipment.Get(Equip::SLOT_ENGINE);
	float hyperclass = EquipType::types[t].pval;
	m_stats.hyperspace_range = 200 * hyperclass * hyperclass / m_stats.total_mass;
	return &m_stats;
}

void Ship::Blastoff()
{
	if (m_flightState != LANDED) return;

	ClearThrusterState();
	m_flightState = FLYING;
	m_testLanded = false;
	m_dockedWith = 0;
	m_launchLockTimeout = 1.0; // one second of applying thrusters

	Enable();
	const double planetRadius = 0.1 + GetFrame()->m_astroBody->GetRadius();
	vector3d up = vector3d::Normalize(GetPosition());
	dBodySetLinearVel(m_body, 0, 0, 0);
	dBodySetAngularVel(m_body, 0, 0, 0);
	dBodySetForce(m_body, 0, 0, 0);
	dBodySetTorque(m_body, 0, 0, 0);
	
	Aabb aabb;
	GetAabb(aabb);
	// XXX hm. we need to be able to get sbre aabb
	SetPosition(up*planetRadius - aabb.min.y*up);
	SetThrusterState(ShipType::THRUSTER_TOP, 1.0f);
}

void Ship::TestLanded()
{
	m_testLanded = false;
	if (m_launchLockTimeout != 0) return;
	if (m_wheelState != 1.0) return;
	if (GetFrame()->m_astroBody) {
		const dReal *vel = dBodyGetLinearVel(m_body);
		double speed = vector3d(vel[0], vel[1], vel[2]).Length();
		const double planetRadius = GetFrame()->m_astroBody->GetRadius();

		if (speed < MAX_LANDING_SPEED) {
			// orient the damn thing right
			// Q: i'm totally lost. why is the inverse of the body rot matrix being used?
			// A: NFI. it just works this way
			matrix4x4d rot;
			GetRotMatrix(rot);
			matrix4x4d invRot = rot.InverseOf();

			vector3d up = vector3d::Normalize(GetPosition());

			// check player is sortof sensibly oriented for landing
			const double dot = vector3d::Dot( vector3d::Normalize(vector3d(invRot[1], invRot[5], invRot[9])), up);
			if (dot > 0.99) {

				Aabb aabb;
				GetAabb(aabb);

				// position at zero altitude
				SetPosition(up * (planetRadius - aabb.min.y));

				vector3d forward = rot * vector3d(0,0,1);
				vector3d other = vector3d::Normalize(vector3d::Cross(up, forward));
				forward = vector3d::Cross(other, up);

				rot = matrix4x4d::MakeRotMatrix(other, up, forward);
				rot = rot.InverseOf();
				SetRotMatrix(rot);

				dBodySetLinearVel(m_body, 0, 0, 0);
				dBodySetAngularVel(m_body, 0, 0, 0);
				dBodySetForce(m_body, 0, 0, 0);
				dBodySetTorque(m_body, 0, 0, 0);
				// we don't use DynamicBody::Disable because that also disables the geom, and that must still get collisions
				dBodyDisable(m_body);
				ClearThrusterState();
				m_flightState = LANDED;
			}
		}
	}
}

void Ship::TimeStepUpdate(const float timeStep)
{
	DynamicBody::TimeStepUpdate(timeStep);

	m_dockingTimer = (m_dockingTimer-timeStep > 0 ? m_dockingTimer-timeStep : 0);

	m_launchLockTimeout -= timeStep;
	if (m_launchLockTimeout < 0) m_launchLockTimeout = 0;
	/* can't orient ships in SetDockedWith() because it gets
	 * called from ode collision handler, and body is locked
	 * and can't be positioned. instead we do it every fucking
	 * update which is retarded but hey */
	if (m_dockedWith) m_dockedWith->OrientDockedShip(this, m_dockedWithPort);

	const ShipType &stype = GetShipType();
	for (int i=0; i<ShipType::THRUSTER_MAX; i++) {
		float force = timeStep * stype.linThrust[i] * m_thrusters[i];
		switch (i) {
		case ShipType::THRUSTER_REAR: 
		case ShipType::THRUSTER_FRONT:
			dBodyAddRelForce(m_body, 0, 0, force); break;
		case ShipType::THRUSTER_TOP:
		case ShipType::THRUSTER_BOTTOM:
			dBodyAddRelForce(m_body, 0, force, 0); break;
		case ShipType::THRUSTER_LEFT:
		case ShipType::THRUSTER_RIGHT:
			dBodyAddRelForce(m_body, force, 0, 0); break;
		}
	}
	dBodyAddRelTorque(m_body, stype.angThrust*m_angThrusters[0],
				  stype.angThrust*m_angThrusters[1],
				  stype.angThrust*m_angThrusters[2]);
	// lasers
	for (int i=0; i<ShipType::GUNMOUNT_MAX; i++) {
		// free old temp laser geoms
		if (m_tempLaserGeom[i]) dGeomDestroy(m_tempLaserGeom[i]);
		m_tempLaserGeom[i] = 0;
		if (!m_gunState[i]) continue;
		dGeomID ray = dCreateRay(GetFrame()->GetSpaceID(), 10000);
		const vector3d pos = GetPosition();
		const vector3f _dir = stype.gunMount[i].dir;
		vector3d dir = vector3d(_dir.x, _dir.y, _dir.z);
		matrix4x4d m;
		GetRotMatrix(m);
		dir = m.ApplyRotationOnly(dir);
		dGeomRaySet(ray, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z);
		dGeomSetData(ray, static_cast<Object*>(&m_laserCollisionObj));
		m_tempLaserGeom[i] = ray;
	}

	if (m_wheelTransition != 0.0f) {
		m_wheelState += m_wheelTransition*timeStep;
		m_wheelState = CLAMP(m_wheelState, 0, 1);
		if ((m_wheelState == 0) || (m_wheelState == 1)) m_wheelTransition = 0;
	}

	if (m_testLanded) TestLanded();
}

void Ship::NotifyDeath(const Body* const dyingBody)
{
	if(GetNavTarget() == dyingBody)
		SetNavTarget(0);
	if(GetCombatTarget() == dyingBody)
		SetCombatTarget(0);
}

const ShipType &Ship::GetShipType()
{
	return ShipType::types[m_shipType];
}

void Ship::SetDockedWith(SpaceStation *s, int port)
{
	if (m_dockedWith && !s) {
		m_dockedWith->OrientLaunchingShip(this, port);
		Enable();
		m_dockedWith = 0;
	} else {
		m_dockedWith = s;
		m_dockedWithPort = port;
		m_dockingTimer = 0.0f;
		m_wheelState = 1.0f;
		if (s->IsGroundStation()) m_flightState = LANDED;
		SetVelocity(vector3d(0,0,0));
		SetAngVelocity(vector3d(0,0,0));
		Disable();
	}
}

void Ship::SetGunState(int idx, int state)
{
	m_gunState[idx] = state;
}

bool Ship::SetWheelState(bool down)
{
	if (m_flightState != FLYING) return false;
	if (down) m_wheelTransition = 1;
	else m_wheelTransition = -1;
	return true;
}

void Ship::SetNavTarget(Body* const target)
{
	m_navTarget = target;
	Pi::worldView->UpdateCommsOptions();
}

void Ship::SetCombatTarget(Body* const target)
{
	m_combatTarget = target;
	Pi::worldView->UpdateCommsOptions();
}

bool Ship::IsFiringLasers()
{
	for (int i=0; i<ShipType::GUNMOUNT_MAX; i++) {
		if (m_gunState[i]) return true;
	}
	return false;
}

/* Assumed to be at model coords */
void Ship::RenderLaserfire()
{
	const ShipType &stype = GetShipType();
	glDisable(GL_LIGHTING);
	for (int i=0; i<ShipType::GUNMOUNT_MAX; i++) {
		if (!m_gunState[i]) continue;
		glPushAttrib(GL_CURRENT_BIT | GL_LINE_BIT);
		glColor3f(1,0,0);
		glLineWidth(2.0f);
		glBegin(GL_LINES);
		vector3f pos = stype.gunMount[i].pos;
		glVertex3f(pos.x, pos.y, pos.z);
		glVertex3fv(&((10000)*stype.gunMount[i].dir)[0]);
		glEnd();
		glPopAttrib();
	}
	glEnable(GL_LIGHTING);
}


static void render_coll_mesh(const CollMesh *m)
{
	glDisable(GL_LIGHTING);
	glColor3f(1,0,1);
	glBegin(GL_TRIANGLES);
	for (int i=0; i<m->ni; i+=3) {
		glVertex3fv(&m->pVertex[3*m->pIndex[i]]);
		glVertex3fv(&m->pVertex[3*m->pIndex[i+1]]);
		glVertex3fv(&m->pVertex[3*m->pIndex[i+2]]);
	}
	glEnd();
	glColor3f(1,1,1);
	glDepthRange(0,1.0f-0.0002f);
	for (int i=0; i<m->ni; i+=3) {
		glBegin(GL_LINE_LOOP);
		glVertex3fv(&m->pVertex[3*m->pIndex[i]]);
		glVertex3fv(&m->pVertex[3*m->pIndex[i+1]]);
		glVertex3fv(&m->pVertex[3*m->pIndex[i+2]]);
		glEnd();
	}
	glDepthRange(0.0,1.0);
	glEnable(GL_LIGHTING);
}

void Ship::Render(const Frame *camFrame)
{
	if ((!dBodyIsEnabled(m_body)) && !m_flightState) return;
	const ShipType &stype = GetShipType();
	params.angthrust[0] = m_angThrusters[0];
	params.angthrust[1] = m_angThrusters[1];
	params.angthrust[2] = m_angThrusters[2];
	params.linthrust[0] = m_thrusters[ShipType::THRUSTER_RIGHT] - m_thrusters[ShipType::THRUSTER_LEFT];
	params.linthrust[1] = m_thrusters[ShipType::THRUSTER_TOP] - m_thrusters[ShipType::THRUSTER_BOTTOM];
	params.linthrust[2] = m_thrusters[ShipType::THRUSTER_REAR] - m_thrusters[ShipType::THRUSTER_FRONT];
	params.pAnim[ASRC_SECFRAC] = Pi::GetGameTime();
	params.pAnim[ASRC_MINFRAC] = Pi::GetGameTime() / 60;
	params.pAnim[ASRC_HOURFRAC] = Pi::GetGameTime() / 3600.0f;
	params.pAnim[ASRC_DAYFRAC] = Pi::GetGameTime() / (24*3600.0f);
	params.pAnim[ASRC_GEAR] = m_wheelState;
	params.pFlag[AFLAG_GEAR] = m_wheelState != 0.0f;
	strncpy(params.pText[0], GetLabel().c_str(), sizeof(params.pText));
	RenderSbreModel(camFrame, stype.sbreModel, &params);

	if (IsFiringLasers()) {
		glPushMatrix();
		TransformToModelCoords(camFrame);
		RenderLaserfire();
		glPopMatrix();
	}
}
