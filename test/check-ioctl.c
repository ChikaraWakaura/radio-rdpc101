#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <linux/videodev2.h>

#define CLEAR(x)	(memset(&(x), 0, sizeof(x)))


int	main( int argc, char **argv )
{
	struct v4l2_capability		v4l2_capa;
	struct v4l2_tuner		v4l2_tuner;
	struct v4l2_frequency		v4l2_freq;
	struct v4l2_frequency_band	v4l2_band;
	char	*dev_name = "/dev/radio0";
	int	fd = -1, result, i;

	fd = open( dev_name, O_RDWR | O_NONBLOCK, 0 );
	if ( fd < 0 ) {
		perror( "Cannot open device" );
		return 1;
	}

	CLEAR( v4l2_capa );
	CLEAR( v4l2_tuner );
	CLEAR( v4l2_freq );
	CLEAR( v4l2_band );

	printf( "check VIDIOC_QUERYCAP\n" );
	result = ioctl( fd, VIDIOC_QUERYCAP, &v4l2_capa );
	if ( result < 0 ) {
		printf( "ng\n" );
	}

	printf( "check VIDIOC_G_TUNER\n" );
	result = ioctl( fd, VIDIOC_G_TUNER, &v4l2_tuner );
	if ( result < 0 ) {
		printf( "ng\n" );
	} else {
		printf( "check VIDIOC_S_TUNER\n" );
		result = ioctl( fd, VIDIOC_S_TUNER, &v4l2_tuner );
		if ( result < 0 )
			printf( "ng\n" );
	}

	printf( "check VIDIOC_G_FREQUENCY\n" );	
	result = ioctl( fd, VIDIOC_G_FREQUENCY, &v4l2_freq );
	if ( result < 0 ) {
		printf( "ng\n" );
	} else {
		printf( "check VIDIOC_S_FREQUENCY\n" );	
		result = ioctl( fd, VIDIOC_S_FREQUENCY, &v4l2_freq );
		if ( result < 0 )
			printf( "ng\n" );
	}

	printf( "check VIDIOC_ENUM_FREQ_BANDS\n" );
	i = -1;
	while ( 1 ) {
		v4l2_band.tuner = v4l2_tuner.index;
		v4l2_band.type  = v4l2_tuner.type;
		v4l2_band.index = i + 1;
		result = ioctl( fd, VIDIOC_ENUM_FREQ_BANDS, &v4l2_band );
		if ( result < 0 )
			break;
		i++;
	}
	if ( i == -1 )
		printf( "ng\n" );

	close( fd );

	printf( "done.\n" );

	return 0;
}
