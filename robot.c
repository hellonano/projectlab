#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <getopt.h>     
#include <sys/sendfile.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>            
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <linux/videodev2.h>
#include <wiringPi.h>

#define CLEAR(x) memset(&(x), 0x00, sizeof(x))
#define PORT 7777		//	소켓 연결에 사용되는 포트

/*		V4L2 		*/
#define WIDTH 640
#define HEIGHT 480 
#define HEADERFRAME1 0xaf
#define DHT_SIZE 420
#define TIMEOUT_CAPTURE 5
#define TIMEOUT_CONNECT_SOCKET 120
#define TIMEOUT_RECV 5
#define TIMEOUT_SEND 5

/*		Motor 		*/
#define PIN_EN1 1
#define PIN_DIRA1 4
#define PIN_DIRB1 5
#define PIN_EN2 7
#define PIN_DIRA2 0
#define PIN_DIRB2 2
#define PIN_LED 3
#define FORWARD 1
#define BACKWARD 2
#define LEFT 3
#define RIGHT 4
#define STOP 5

/*		Socket 		*/
#define BUF_SIZE_RECV 1
#define BUF_SIZE_SEND 250

/*		Functions	*/
static void errno_print(const char *s);					//	에러 메세지 출력
static void _shutdown(void);							//	항체 종료
static void blink_led(void);							//	LED 점멸

/*		Functions V4L2		*/
static int xioctl(int fh, int request, void *arg);		//	디바이스(웹캠) 환경설정
static int open_webcam(void);							//	웹캠 오픈
static int init_webcam(void);							//	웹캠 초기화(환견 설정)
static int set_format(int height, int weight);			//	캡쳐 포멧 지정
static int init_mmap(void);								//	MMAP 초기화
static void start_capture(void);						//	웹캠 캡쳐 시작
static int read_frame(void);							//	웹캠 버퍼 읽기
static void uninit_mmap(void);							//	MMAP 해제
static void close_webcam(void);							//	웹캠 닫기
static void stop_capture(void);							//	웹캠 챕쳐 중지
static void process_image(const void *p, int size);		//	MJPEG -> JPEG 변환	

/*		Functions Motor		*/
static int init_motor(void);							//	모터 초기화
static void run_motor(int direction);					//	모터 구동 (방향에 따른)

/*		Functions Socket	*/
static int waitClient(void);							//	소켓 연결 대기
static int setupSocket(void);							//	소켓 초기화
static int runThread(void);								//	스레드 생성 및 구동
static void *thread_motor(void *arg);					//	모터 스레드 수행 함수
static void *thread_webcam(void *arg);					//	웹캠 스레드 수행 함수
static int send_image(int size);						//	캡쳐 이미지 전송

static const char *dev_name = "/dev/video0";			//	웹캠 디바이스 경로

//	흐름 제어 변수
int endPoint = -1;
int endPointRecv = -1;
int endPointSend = -1;
int is_cam_error = -1;


//	버퍼 변수
struct buffer {											//	웹캠 할당 버퍼
	void *start;
	size_t length;
};
struct buffer *buffers;
static int webcam_fd = -1;
static unsigned int n_buffers;
static unsigned char *tmpbuffer;

//	소켓 변수
struct sockaddr_in listen_addr, client_addr;
int listen_sockfd, server_sockfd;

//	스레드 변수
pthread_mutex_t m_lock;
pthread_t pthread_recv, pthread_send;

//	JPEG 헤더 데이터
static unsigned char dht_data[DHT_SIZE] = {
    0xff, 0xc4, 0x01, 0xa2, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x01, 0x00, 0x03,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05,
    0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04,
    0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22,
    0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15,
    0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95,
    0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
    0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2,
    0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5,
    0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
    0xfa, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05,
    0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
    0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
    0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
    0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa
};

int main() {

	while(1) {
		/*
			모터 초기화
			웹캠 초기화 (디바이스 열기, 설정, MMAP)
			초기화 실패 -> 종료
		*/
		is_cam_error = -1;
		endPointRecv = -1;
		endPointSend = -1;
		endPoint = -1;
		if(init_motor() == -1 || open_webcam() == -1 || init_webcam() == -1) {
			printf("Init Fail\n");
			close_webcam();
			_shutdown();
			break;
		}

		/*
			소켓 초기화 실패 -> 항체 종료
			1차 소켓 초기화 실패 -> 네트워크 재시작
			2차 소켓 초기화 실패 -> 항체 종료
		*/
		if( setupSocket() != -1 ) {
			if( waitClient() == -1) {
				system("/etc/init.d/networking restart");
				if( waitClient() == -1) {
					_shutdown();
					break;
				}
			}
		}else {
			_shutdown();
			break;
		}

		/*
			스레드 시작 실패 -> 항체 종료
		*/
		if( runThread() == -1 ) {
			uninit_mmap();
			close_webcam();
			close(listen_sockfd);
			close(server_sockfd);
			_shutdown();
		}

		/*
			메인 스레드 : 캡쳐 및 전송, 동작 데이터 수신 스레드 중지 확인
			스레드 모두 종료 -> 모터 도작 정지 & 스레드 종료
		*/
		while(1) {
			if(endPointRecv == 1 && endPointSend == 1) {
				run_motor(STOP);
				pthread_join(pthread_recv, NULL);
				pthread_join(pthread_send, NULL);
				break;
			}
		}
	 	
	 	/*
	 		사용 자원 해제
	 	*/

		uninit_mmap();
		close_webcam();
		close(listen_sockfd);
		close(server_sockfd);
		printf("------------------------------------------\n");
		
		/*
			웹캠의 문제로 스레드가 정지하면 연결 대기 없이 항체 종료
		*/
		if(is_cam_error == 1)
			_shutdown();
	}
	return 0;
}

static int runThread(void) {
	

	if( (pthread_mutex_init(&m_lock, NULL)) !=0 ) {
		perror("pthread_mutext_init");
		return -1;
	}

	if( (pthread_create(&pthread_recv, NULL, thread_motor, NULL)) != 0 ) {
		perror("[System] thread_motor Create Error");
		return -1;
	} else
		printf("[System] thread_motor Start\n");

	if( (pthread_create(&pthread_send, NULL, thread_webcam, NULL)) != 0 )	{
		perror("[System] thread_webcam Create Error");
		return -1;
	} else
		printf("[System] thread_webcam Start\n");

	return 0;
}

static void *thread_webcam(void *arg) {
	printf("[Webcam] thread_webcam Start.\n");
	char socket_buffer[1024];
	int sn;
	int count = 0;
	fd_set webcam_fds;
	struct timeval  tv;
	int r;
	int size_read;

	start_capture();
	while(1) {
		if(endPointRecv == 1) {
			break;
		}
		FD_ZERO(&webcam_fds);
		FD_SET(webcam_fd, &webcam_fds);
		tv.tv_sec = TIMEOUT_CAPTURE;
		tv.tv_usec = 0;	
		r = select(webcam_fd + 1, &webcam_fds, NULL, NULL, &tv);

		if( r == -1 ) {
			errno_print("[Webcam] select() == -1");
			break;
		}

		if( r == 0 ) {
			fprintf(stderr, "[Webcam] Timeout\n");
			break;
		}
		if( (size_read = read_frame()) < 0) {
			printf("[Webcam] read_frame() < 0\n");
			break;
		}

		if(send_image(size_read)) {
			printf("[Webcam] send_image() = 0\n");
			break;
		}
	}

	run_motor(STOP);

	stop_capture();
	printf("[Webcam] thread_webcam Stop.\n");
	endPointSend = 1;
	return;
}

void *thread_motor(void *arg) {
	printf("[Motor] thread_motor Start.\n");
	char socket_buffer;
	int r;
	int rn;
	int exitLoop = -1;

	struct timeval tv;
	fd_set fds;
	while( 1 ) {
		if(endPointSend == 1)
			break;
		
		tv.tv_sec = TIMEOUT_RECV;
		tv.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(server_sockfd, &fds);
		r = select(server_sockfd + 1, &fds, NULL, NULL, &tv);
		if( r == -1 ) {
			errno_print("[Motor] select() == -1");
			if( errno == EINTR || errno == EWOULDBLOCK ) 
				continue;
			break;
		}
		if( r == 0 ) {
			fprintf(stderr, "[Motor] Recv Timeout\n");
			continue;
		}
		int i;
		for(i = 0; i<server_sockfd + 1; i++) {
			if(FD_ISSET(i, &fds)) {
				if(i == server_sockfd) {
					if( (rn = recv(server_sockfd, &socket_buffer, BUF_SIZE_RECV, 0)) <= 0) {
						printf("[Motor] recv() <= 0\n");
						if( errno == EWOULDBLOCK || errno == EAGAIN ) {
							break;
						}
						exitLoop = 1;
						break;
					}	
					
					char cmd;
					sscanf(&socket_buffer, "%c", &cmd);

					if( cmd == 'f' ) 
						run_motor(FORWARD);
					else if( cmd == 'l' ) 
						run_motor(LEFT);
					else if( cmd == 'r' ) 
						run_motor(RIGHT);
					else if( cmd == 'b' ) 
						run_motor(BACKWARD);
					else if( cmd == 's' ) 
						run_motor(STOP);
					else if( cmd == 'q') {
						exitLoop = 1;
						break;
					}else printf("[Motor] Unexpectied Word Recv\n");
				}
			}
		}
		if(exitLoop == 1)
			break;
	}
	run_motor(STOP);
	printf("[Motor] thread_motor Stop.\n");
	endPointRecv = 1;
	return;
}

static int send_image(int size) {
	struct stat stat_buf;
	off_t offset = 0;
	int sn;
	int n_size = 0;
	int n_size_file = 0;
	int n_size_send = 0;

	struct timeval tv;
	fd_set fds;
    FD_ZERO(&fds);
    
    int r;

	int filesize = htonl(size);
	
	FD_SET(server_sockfd, &fds);
	tv.tv_sec = TIMEOUT_SEND;
   	tv.tv_usec = 0;
   	r = select(server_sockfd + 1, NULL, &fds, NULL, &tv);
   	if(r == 0) {
   		return 1;
	}else if(r == -1) {
	        printf("select error : %d\n", errno);
	}else{	
	    if(FD_ISSET(server_sockfd, &fds)) {
	    	if( (sn = send(server_sockfd, &filesize, sizeof(filesize), MSG_NOSIGNAL)) <= 0 ) {
	    		errno_print("[Webcam] send_image() in thread_webcam() exit contition >> Image Size send() <= 0\n");
				return 1;
			}
	    }
	}

	while(n_size_send < size) {
		if(endPointRecv == 1) {
			printf("[Webcam] send image in thread_webcam exit contition >> endPointrecv == 1\n");
			return 1;
		}
   		n_size_file = n_size_file + n_size;
   		FD_SET(server_sockfd, &fds);
		tv.tv_sec = TIMEOUT_SEND;
   		tv.tv_usec = 0;
   		r = select(server_sockfd + 1, NULL, &fds, NULL, &tv);
   		if(r == 0)
	    {
	    	return 1;
	    }
	    else if(r == -1)
	    {
	        printf("select error : %d\n", errno);
	    }
	    else
	    {	
	        if(FD_ISSET(server_sockfd, &fds))
	        {
	            if( (sn = send(server_sockfd, &tmpbuffer[n_size_send], n_size_send < size - 1024 ? 1024 : size - n_size_send, MSG_NOSIGNAL)) <= 0 ) {
	            	errno_print("[Webcam] send image in thread_webcam exit contition >> Image send() <= 0\n");
		    		if( sn == 0 ) {
		    			printf("sn = 0\n");
					}
					if( errno == EWOULDBLOCK || errno == EAGAIN ) 
						return 0;	// Error나면 여기 지워보기

					if(errno == EPIPE) {
						}
					return 1;
				}

	        }else printf("FD ISSET FALSE\n");
	    }
		n_size_send = n_size_send + sn;
	}
    return 0;
}

static int read_frame(void) {
	struct v4l2_buffer buf;
	unsigned int i;
	CLEAR(buf);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if( xioctl(webcam_fd, VIDIOC_DQBUF, &buf) ) {
		switch (errno) {
			case EAGAIN :
				printf("EAGAIN\n");
				is_cam_error = 1;
				return -1;
			case EIO : 
			break;
			default :
				errno_print("VIDIOC_DQBUF");
				is_cam_error = 1;
				return -1;
		}
	}

	assert( buf.index < n_buffers );
	memcpy (tmpbuffer, buffers[buf.index].start, HEADERFRAME1);
    memcpy (tmpbuffer + HEADERFRAME1, dht_data, DHT_SIZE);
    memcpy (tmpbuffer + HEADERFRAME1 + DHT_SIZE, buffers[buf.index].start + HEADERFRAME1, buf.bytesused - HEADERFRAME1);
	if( xioctl(webcam_fd, VIDIOC_QBUF, &buf) == -1 ) {
		errno_print("VIDIOC_QBUF");
		is_cam_error = 1;
		return -1;
	}
	return buf.bytesused + DHT_SIZE;
}

static void run_motor(int direction) {
	switch(direction) {
		case FORWARD :
			digitalWrite(PIN_EN1, HIGH);
			digitalWrite(PIN_EN2, HIGH);
			digitalWrite(PIN_DIRA1, LOW);
			digitalWrite(PIN_DIRB1, HIGH);
			digitalWrite(PIN_DIRA2, LOW);
			digitalWrite(PIN_DIRB2, HIGH);
			break;
		case LEFT :
			digitalWrite(PIN_EN1, HIGH);
			digitalWrite(PIN_EN2, HIGH);
			digitalWrite(PIN_DIRA1, LOW);
			digitalWrite(PIN_DIRB1, HIGH);
			digitalWrite(PIN_DIRA2, HIGH);
			digitalWrite(PIN_DIRB2, LOW);
			break;
		case RIGHT :
			digitalWrite(PIN_EN1, HIGH);
			digitalWrite(PIN_EN2, HIGH);
			digitalWrite(PIN_DIRA1, HIGH);
			digitalWrite(PIN_DIRB1, LOW);
			digitalWrite(PIN_DIRA2, LOW);
			digitalWrite(PIN_DIRB2, HIGH);
			break;
		case BACKWARD :
			digitalWrite(PIN_EN1, HIGH);
			digitalWrite(PIN_EN2, HIGH);
			digitalWrite(PIN_DIRA1, HIGH);
			digitalWrite(PIN_DIRB1, LOW);
			digitalWrite(PIN_DIRA2, HIGH);
			digitalWrite(PIN_DIRB2, LOW);
			break;
		case STOP :
			digitalWrite(PIN_EN1, LOW);
			digitalWrite(PIN_EN2, LOW);
			digitalWrite(PIN_DIRA1, HIGH);
			digitalWrite(PIN_DIRB1, HIGH);
			digitalWrite(PIN_DIRA2, HIGH);
			digitalWrite(PIN_DIRB2, HIGH);
			break; 
	}
}

static void start_capture(void) {
	unsigned int i;
	enum v4l2_buf_type type;

	for( i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if( xioctl(webcam_fd, VIDIOC_QBUF, &buf) == -1 )
			errno_print("VIDIOC_QBUF");
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if( xioctl(webcam_fd, VIDIOC_STREAMON, &type) )
		errno_print("VIDIOC_STREAMON");

	printf("[System] Start Capture\n");
}

static int init_webcam(void) {

	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;

	// V4L2 Device가 맞는지 확인
	if( xioctl(webcam_fd, VIDIOC_QUERYCAP, &cap) ) {
		if( errno = EINVAL ) {
			fprintf(stderr, "%s is no V4L2 device\n", dev_name);
		} else {
			errno_print("VIDIOC_QUERYCAP");
		}
		return -1;
	}

	// VIDEO CAPTURE 디바이스가 맞는지 확인
	if( !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ) {
		fprintf(stderr, "%s is no video capture device\n", dev_name);
		return -1;
	}

	// VIDEO STREAMING 디바이스가 맞는지 확인
	if( !(cap.capabilities & V4L2_CAP_STREAMING) ) {
		fprintf(stderr, "%s dose not support streaming i/o\n", dev_name);
		return -1;
	}

	set_format(WIDTH, HEIGHT);	//	캡쳐 이미지 해상도
	init_mmap();
	printf("[System] Initial Webcam\n");

	return 1;
}

static int set_format(int height, int weight) {
	unsigned int min;

	struct v4l2_format fmt;
	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;	
	
	if ( xioctl(webcam_fd, VIDIOC_G_FMT, &fmt) == -1) {
		errno_print("VIDIOC_G_FMT : pixel format not supported");
		return -1;
	}
	
	fmt.fmt.pix.width = WIDTH;
	fmt.fmt.pix.height = HEIGHT;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;	
	fmt.fmt.pix.colorspace  = V4L2_COLORSPACE_SRGB;

	if ( xioctl(webcam_fd, VIDIOC_S_FMT, &fmt) == -1) {
		errno_print("VIDIOC_S_FMT : pixel format not supported");
		close_webcam();
		return -1;
	}

	min = fmt.fmt.pix.width * 2;
	if( fmt.fmt.pix.bytesperline < min )
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if( fmt.fmt.pix.sizeimage < min )
		fmt.fmt.pix.sizeimage = min;
}

static int init_mmap(void) {
	tmpbuffer = (unsigned char *) calloc (1, (size_t) WIDTH * HEIGHT << 1);
	struct v4l2_requestbuffers req;

	// 버퍼 할당 요구사항 지정
	CLEAR(req);
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	// 버퍼 할당 요청
	if( xioctl(webcam_fd, VIDIOC_REQBUFS, &req) ) {
		if( errno == EINVAL ) {
			fprintf(stderr, "%s doest not support memory mapping\n", dev_name);
		}else 
			errno_print("VIDIOC_REQBUFS");
		return -1;
	}

	// 할당된 버퍼가 요구한 버퍼보다 작을 때
	if(req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
		return -1;
	}

	// 버퍼 메모리 정보를 저장할 구조체에 메모리 할당
	buffers = (struct buffer*)calloc(req.count, sizeof(*buffers));

	// 버퍼 메모리 할당에 실패하면
	if( !buffers ) {
		fprintf(stderr, "Out of memory\n");
		return -1;
	}

	// Memory Map 설정
	for( n_buffers = 0; n_buffers < req.count; ++n_buffers ) {
		struct v4l2_buffer buf;
		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;

		if( xioctl(webcam_fd, VIDIOC_QUERYBUF, &buf) == -1 ) {
			errno_print("VIDIOC_QUERYBUF");
			return -1;
		}

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ, MAP_SHARED, webcam_fd, buf.m.offset);

		if( buffers[n_buffers].start == MAP_FAILED ) {
			errno_print("[Error] mmap");
			return -1;
		}
	}

	printf("[System] Initial MMAP\n");
	return 1;
}

static int init_motor(void) {
	if( wiringPiSetup() == -1 )
		return -1;

	pinMode(PIN_EN1, OUTPUT);	
	pinMode(PIN_EN2, OUTPUT);
	pinMode(PIN_DIRA1, OUTPUT);
	pinMode(PIN_DIRB1, OUTPUT);
	pinMode(PIN_DIRA2, OUTPUT);
	pinMode(PIN_DIRB2, OUTPUT);
	pinMode(PIN_LED, OUTPUT);
	printf("[System] Initial Motor\n");
	return 1;
}

static int setupSocket(void) {
	
	int n;

	if((listen_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		errno_print("[Error] create socket error : ");
		return -1;
	}

	int optval = 1;
	setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));


	memset((void *)&listen_addr, 0x00, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	listen_addr.sin_port = htons(PORT);

	if(bind(listen_sockfd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) == -1) {
		errno_print("[Error] bind");
	}

	if(listen(listen_sockfd, 1) == -1) {
		errno_print("[Error] listen");
	}

	return 0;
}

static int waitClient(void) {
	int i;
	for(i=0; i< 10; i++) {
		digitalWrite(PIN_LED, HIGH);
		delay(100);
		digitalWrite(PIN_LED, LOW);
		delay(100);	
	}
	digitalWrite(PIN_LED, HIGH);
	int r;
	struct timeval tv;
	fd_set fds;
    FD_ZERO(&fds);

	printf("[System] Wait Client\n");
	int client_len = sizeof(client_addr);
	while(1) {

		FD_SET(listen_sockfd, &fds);
		tv.tv_sec = TIMEOUT_CONNECT_SOCKET;
   		tv.tv_usec = 0;
   		r = select(listen_sockfd + 1, &fds, NULL, NULL, &tv);
   		if(r == 0)
	    {
	        printf("[Error] Accept Timeout\n");
	        return -1;
	    }
	    else if(r == -1)
	    {
	        return -1;
	    }
	    else
	    {	
	        if(FD_ISSET(listen_sockfd, &fds)) {
	        	if((server_sockfd = accept(listen_sockfd, (struct sockaddr *)&client_addr, (socklen_t*)&client_len)) <=0 ) {
					printf("[System] Accept Fail\n");
					return -1;
				}
				printf("[System] New Client Connect : %s\n", inet_ntoa(client_addr.sin_addr));

				int flags;
			    int d = fcntl(server_sockfd, F_SETFL, flags | O_NONBLOCK);
	        }
	    }
	    digitalWrite(PIN_LED, LOW);
		return 0;
	}
	
}

static int open_webcam(void) {
	//	웹캠 연결 확인
	struct stat st;
	if( stat(dev_name, &st) ) {
		fprintf(stderr, "Cannot identify '%s' : %d, %s\n", dev_name, errno, strerror(errno));
		return -1;
	}

	if( !S_ISCHR(st.st_mode) ) {
		fprintf(stderr, "%s is no device\n", dev_name);
		return -1;
	}

	webcam_fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
	int fd;
	if( webcam_fd == -1 ) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno, strerror(errno));
		return -1;
	}
	return 1;
}

static void stop_capture(void) {
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if( xioctl(webcam_fd, VIDIOC_STREAMOFF, &type) )
		errno_print("VIDIOC_STREAMOFF");
}

static void uninit_mmap(void) {
	unsigned int i;

	for( i = 0; i < n_buffers; ++i )
		if( munmap(buffers[i].start, buffers[i].length) )
			errno_print("[Error] uninit_mmap");

	free(buffers);
	free(tmpbuffer);
}

static void close_webcam(void) {
	if( close(webcam_fd) == -1 )
		errno_print("[Error] close_webcam");
	webcam_fd = -1;
}

static void errno_print(const char *s) {
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
}

static int xioctl(int fh, int request, void *arg) {
	int r;

	do {
		r = ioctl(fh, request, arg);
		/*
			fh 입출력 지정자, 파일 소켓 등
			request 요청내용
			*arg request를 통해 얻은 정보를 저장하기 위한 버퍼
		*/
	}while( -1 == r && EINTR == errno);

	return r;
}

static void _shutdown(void) {
	run_motor(STOP);
	uninit_mmap();
	close_webcam();
	close(listen_sockfd);
	close(server_sockfd);
	blink_led();
	system("shutdown -h now");
}

static void blink_led(void) {
	int i;
	for(i=0; i< 4; i++) {
		digitalWrite(PIN_LED, HIGH);
		delay(500);
		digitalWrite(PIN_LED, LOW);
		delay(500);	
	}
	for(i=0; i< 8; i++) {
		digitalWrite(PIN_LED, HIGH);
		delay(200);
		digitalWrite(PIN_LED, LOW);
		delay(200);	
	}
	for(i=0; i< 12; i++) {
		digitalWrite(PIN_LED, HIGH);
		delay(50);
		digitalWrite(PIN_LED, LOW);
		delay(50);	
	}
}