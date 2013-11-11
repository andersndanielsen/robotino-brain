#include "headers/_OmniDrive.h"

#include "headers/Axon.h"
#include "headers/Brain.h"
#include "headers/_Odometry.h"

#include "../geometry/Angle.h"
#include "../geometry/AngularCoordinate.h"
#include "../geometry/Vector.h"

#include <rec/robotino/api2/OmniDrive.h>

#include <unistd.h> // Needed by usleep()
#include <stdlib.h>
#include <iostream>
#include <iomanip>	// Manipulation of output
#include <math.h> // For abs()


/// @todo Ressurect travelReversed functionality

_OmniDrive::_OmniDrive( Brain * pBrain )
	: Axon::Axon( pBrain ),
	rec::robotino::api2::OmniDrive::OmniDrive()
{
	this->xSpeed = 0.0;
	this->ySpeed = 0.0;
	this->omega = 0.0;
	this->xOld = 0.0;
	this->yOld = 0.0;
	this->omegaOld = 0.0;

	this->travelReversed = false;
	this->onlyManouver = false;
	this->autoDrive = true;
	this->stop = false;

	this->_destination = (Coordinate) this->brain()->odom()->getPosition();
	this->_pointAt = Coordinate( 0.0, 0.0 );
	this->_doPointAt = false;
	this->_stopWithin = 0.0;
}

Coordinate
_OmniDrive::destination()
{
	// Not sure if this is the best value, however, returning 
	// null object might cause breakage --Lars
	if ( ! this->autoDrive ) return this->brain()->odom()->getPosition();

	return this->_destination;
}

void
_OmniDrive::setDestination( Coordinate destination )
{
	this->autoDrive = true;
	this->_destination = destination;
}

Coordinate
_OmniDrive::pointAt()
{
	return this->_pointAt;
}

void
_OmniDrive::setPointAt( Coordinate target )
{
	if ( ! this->autoDrive )
	{
		this->setDestination( this->brain()->odom()->getPosition() );
		this->autoDrive = true;
	}
	this->_pointAt = target;
	this->_doPointAt = true;
}

bool
_OmniDrive::pointingActive()
{
	return this->_doPointAt;
}

void
_OmniDrive::stopPointing()
{
	this->_doPointAt = false;
}

float
_OmniDrive::stopWithin()
{
	return this->_stopWithin;
}

void
_OmniDrive::setStopWithin( float distance )
{
	if ( distance < OMNIDRIVE_MIN_ACCEPTABLE_DISTANCE )
		this->_stopWithin = OMNIDRIVE_MIN_ACCEPTABLE_DISTANCE;
	else
		this->_stopWithin = distance;
}

void
_OmniDrive::analyze()
{}

void
_OmniDrive::apply()
{
	// Override automatic driving if disabled
	if ( ! this->autoDrive )
	{
//		std::cout << "OmniDrive: apply() aborted, autoDrive disabled" << std::endl;
		return;
	}

	// Preserve old values
	this->xOld = this->xSpeed;
	this->yOld = this->ySpeed;
	this->omegaOld = this->omega;

	// Initial values = stop
	this->xSpeed = 0.0;
	this->ySpeed = 0.0;
	this->omega = 0.0;

	if ( ! this->stop )
	{
		// Aquire position and destination
		AngularCoordinate position = this->brain()->odom()->getPosition();
		Coordinate destination = this->destination();
		Vector destinationVector = position.getVector( destination );

		// Calculate driving speed
		if ( this->onlyManouver || destinationVector.magnitude() < OMNIDRIVE_TRAVEL_MIN_DISTANCE )
			this->manouverTowards( (Angle) position, destinationVector );
		else
			this->travelTowards( destinationVector );

		// Calculate turning speed if not driving
		if ( this->pointingActive() && destinationVector.magnitude() <
				( OMNIDRIVE_POINTING_DESTINATION_MAX_DISTANCE - this->_stopWithin ) )
			this->turnTowards( position, _pointAt );
	}
	
	// Apply soft accelleration
	this->xSpeed = this->softAccellerate( this->xSpeed, this->xOld );
	this->ySpeed = this->softAccellerate( this->ySpeed, this->yOld );
	this->omega = this->softAccellerate( this->omega, this->omegaOld, true );
	
	// Apply velocities
//	std::cout
//		<< "OmniDrive set: "
//		<< this->xSpeed << " "
//		<< this->ySpeed << " "
//		<< this->omega
//		<< std::endl;

	rec::robotino::api2::OmniDrive::setVelocity( xSpeed, ySpeed, omega );
}

void
_OmniDrive::niceStop()
{
	std::cout << "OmniDrive: performing a nice stop" << std::endl;
	this->stop = true;
}

void
_OmniDrive::fullStop()
{
	std::cout << "OmniDrive: performing emergency full stop" << std::endl;
	rec::robotino::api2::OmniDrive::setVelocity( 0.0, 0.0, 0.0 );
	this->xSpeed = 0.0;
	this->ySpeed = 0.0;
	this->omega = 0.0;
	this->xOld = 0.0;
	this->yOld = 0.0;
	this->omegaOld = 0.0;
	this->stop = true;
}

bool
_OmniDrive::stopIsSet()
{
	return this->stop;
}

void
_OmniDrive::go()
{
	std::cout << "Omnidrive: resuming drive" << std::endl; 
	this->stop = false;
}

void
_OmniDrive::setVelocity( float xSpeed, float ySpeed, float omega )
{
	if ( this->stop )
	{
		std::cout << "OmniDrive: cannot set speed, stop order in effect" << std::endl;
		return;
	}

	if ( this->autoDrive )
	{
		this->autoDrive = false;
		std::cout << "Switched to manual drive mode" << std::endl;
	}
	// TODO Use smooth accelleration? Target speeds?
	rec::robotino::api2::OmniDrive::setVelocity( xSpeed, ySpeed, omega );
}


// PRIVATE FUNCTIONS

void
_OmniDrive::travelTowards( Vector destinationVector )
{
	if ( destinationVector.magnitude() < this->_stopWithin ) return;

	// Calculate new turn and drive speeds
	Angle deltaAngle = this->brain()->odom()->getPosition().deltaAngle( destinationVector );
	if ( this->travelReversed ) deltaAngle.reverse();

	this->xSpeed = findTravelVelocity( destinationVector.magnitude(), deltaAngle.phi() );
	this->omega = findAngularVelocity( deltaAngle.phi() );
}

void
_OmniDrive::manouverTowards( Angle heading, Vector destinationVector )
{
	if ( destinationVector.magnitude() < this->_stopWithin ) return;

	destinationVector.setPhi( heading.deltaAngle( destinationVector ).phi() );
	Coordinate cartesian = destinationVector.cartesian();

	this->xSpeed = this->findManouverVelocity( cartesian.x() );
	this->ySpeed = this->findManouverVelocity( cartesian.y() );
}

void
_OmniDrive::turnTowards( AngularCoordinate position, Coordinate target ) 
{
	Vector targetVector = position.getVector( target );
	if ( targetVector.magnitude() <
			( OMNIDRIVE_POINTING_TARGET_MIN_DISTANCE + _stopWithin ) ) return;

	Angle deltaAngle = position.deltaAngle( targetVector );

//	std::cout
//		<< "OmniDrive position       : " << position
//		<< "\nOmniDrive turning towards: " << target
//		<< "\nOmniDrive turn deltaAngle: " << deltaAngle << std::endl;

	if ( fabs( deltaAngle.phi() ) > OMNIDRIVE_ROTATE_ACCEPTABLE_DELTA_ANGLE )
		this->omega = findAngularVelocity( deltaAngle.phi() );
	else
		this->_doPointAt = false;
}

float
_OmniDrive::findManouverVelocity( float length )
{
	/// @todo better math could be applied here
	float speed = length * 1.3;
	if ( speed > OMNIDRIVE_MANOUVER_MAX_SPEED ) return OMNIDRIVE_MANOUVER_MAX_SPEED;
	if ( speed > 0 && speed < OMNIDRIVE_MANOUVER_MIN_SPEED ) return OMNIDRIVE_MANOUVER_MIN_SPEED;
	return speed;
}

float
_OmniDrive::findTravelVelocity( float length, float deltaAngle )
{
	if ( fabs( deltaAngle ) > OMNIDRIVE_TRAVEL_MAX_ANGLE ) return 0.0;

	/// @todo better math could be applied here
	float speed = length * 1.3 * (( OMNIDRIVE_TRAVEL_MAX_ANGLE - fabs( deltaAngle )) / OMNIDRIVE_TRAVEL_MAX_ANGLE );
	if ( speed > OMNIDRIVE_TRAVEL_MAX_SPEED ) return OMNIDRIVE_TRAVEL_MAX_SPEED;
	if ( speed > 0 && speed < OMNIDRIVE_TRAVEL_MIN_SPEED ) return OMNIDRIVE_TRAVEL_MIN_SPEED;
	return speed;
}

float
_OmniDrive::findAngularVelocity( float deltaAngle )
{
	/// @todo better math could be applied here
	deltaAngle *= 1.5;
	float abs = fabs( deltaAngle );
	if ( abs > OMNIDRIVE_ROTATE_MAX_SPEED )
		return ( ( deltaAngle > 0 ) ? OMNIDRIVE_ROTATE_MAX_SPEED : - OMNIDRIVE_ROTATE_MAX_SPEED );
	if ( abs < ( OMNIDRIVE_ROTATE_MIN_SPEED / 4 ) ) return 0.0f;
	if ( abs < OMNIDRIVE_ROTATE_MIN_SPEED )
		return ( deltaAngle > 0 ) ? OMNIDRIVE_ROTATE_MIN_SPEED : - OMNIDRIVE_ROTATE_MIN_SPEED;
	return deltaAngle;
}

float
_OmniDrive::softAccellerate( float newSpeed, float currentSpeed, bool isRotation )
{
	float deltaSpeed = newSpeed - currentSpeed;
	float maxSpeedAdjust = ( isRotation ) ? OMNIDRIVE_ROTATE_MAX_ADJUST : OMNIDRIVE_VELOCITY_MAX_ADJUST;
	if ( fabs( deltaSpeed ) > maxSpeedAdjust )
		return ( deltaSpeed > 0 ) ? currentSpeed + maxSpeedAdjust : currentSpeed - maxSpeedAdjust;
	return newSpeed;
}

