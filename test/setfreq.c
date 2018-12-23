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
#define FREQ_MHZ_MUL	(10000000 / 625)
#define	FREQ_KHZ_MUL	(10000 / 625)


int	main( int argc, char **argv )
{
	struct v4l2_frequency	v4l2_freq;
	char	*dev_name = "/dev/radio0";
	int	fd = -1;
	int	result;
	double	req_freq;

	if ( argc != 2 )
	{
		return 1;
	}
	req_freq = atof( argv[1] );

	fd = open( dev_name, O_RDWR | O_NONBLOCK, 0 );
	if ( fd < 0 ) {
		perror( "open" );
		return 1;
	}

	CLEAR( v4l2_freq );
	
	result = ioctl( fd, VIDIOC_G_FREQUENCY, &v4l2_freq );
	if ( result < 0 ) {
		perror( "ioctl" );
	} else {
		if ( req_freq >= 522 && req_freq <= 1629 )
			v4l2_freq.frequency = req_freq * FREQ_KHZ_MUL;
		else
			v4l2_freq.frequency = req_freq * FREQ_MHZ_MUL;
		result = ioctl( fd, VIDIOC_S_FREQUENCY, &v4l2_freq );
		if ( result < 0 )
			perror("ioctl");
	}

	close( fd );

	return 0;
}
