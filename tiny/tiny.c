/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// argv: 문자열 포인터의 배열
int main(int argc, char **argv) {
  // listenfd: 리스팅 소켓 파일 드스크립터, connfd: 클라와 연결을 위한 소켓 파일 디스크립터
  int listenfd, connfd;
  // 호스트 및 포트 저아할 버퍼
  char hostname[MAXLINE], port[MAXLINE];
  // 클라 주소 길이 저장 변수
  socklen_t clientlen;
  // 클라 주소 저장할 구조체
  struct sockaddr_storage clientaddr;

  /* 명령줄 인수 확인 (하나의 인수만 요구 (제목 포함 2))*/
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]); // argv[0]은 실행된 프로그램의 이름
    exit(1);
  }

  // 리스닝 소켓 생성 
  listenfd = Open_listenfd(argv[1]);
  // 무한루프 안에서 클라 연결을 계속 수락, clientaddr 에는 클라의 주소 정보가 저장
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    // 클라 정보 출력: clientaddr에서 호스트, 포트 추출해서 hostname, port에 저장
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
    port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // 클라이언트 요청 처리
    Close(connfd);  // 클라이언트 연결 종료
  }
}

// 서버에서 클라이언트의 HTTP 요청을 처리 역할.
// 요청 분석하고, 정적 파일을 제공하거나 CGI 프로그램 실행하여 동적 컨텐츠 생성
void doit(int fd)
{
  printf("doit함수호출\n");
  int is_static;          // 동적 정적?
  struct stat sbuf;       // 파일의 상태 정보를 담는 stat 구조체

  // buf: HTTP 요청 라인을 저장하는 버퍼 
  // method, uri, version: 요청의 메소드(GET, POST등),URI, HTTP 버전을 저장하는 버퍼
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  // 요청된 파일 이름과 CGI 인수 저장 버퍼
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;              // 리드 연산을 위한 구조체

  Rio_readinitb(&rio,fd);             // rio 구조체 초기화
  Rio_readlineb(&rio, buf, MAXLINE); // 요청 라인 읽고 buf에 저장
  printf("요청라인:%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);  // 메소드, URI, HTTP 버전 추출

  // 요청 메소드가 GET이 아니면 오류 반환하고 종료
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // 요청 헤더 읽기:
  printf("Request headers:\n");
  read_requesthdrs(&rio); 

  // parse_uri 호출해서 URI분석 후, 파일이름, CGI 인수 추출
  // is_static을 설정하여 정적인지 동적인지 반환
  is_static = parse_uri(uri,filename,cgiargs);

  if (stat(filename, &sbuf) < 0) { //파일이 디스크 상에 없을 경우
    clienterror(fd, filename, "404", "Not found","Tiny couldn’t find this file");
    return;
  }
  

  // 동적 정적? 인지 구별
  if (is_static) {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //보통파일인지? 이 파일을 읽을 권한을 가지고 있는지?
      clienterror(fd, filename, "403", "Forbidden","Tiny couldn’t read the file");
      return;
    }
    serve_static(fd,filename,sbuf.st_size); //정적 컨텐츠 제공
  }
  else {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { // 보통파일인지? 실행가능한지? 
      clienterror(fd, filename, "403", "Forbidden","Tiny couldn’t run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);  //동적 컨텐츠 제공
  }
}

// HTTP 에러 응답을 클라이언트에 전송하는 역할
void clienterror(int fd, char *cause, char *errnum,char *shortmsg, char *longmsg)
{
  // buf: HTTP 헤더와 관련 정보 저장 버퍼, body: HTML 에러 메시지 포함할 버퍼
  char buf[MAXLINE] , body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  // 상태 줄을 작성 (ex. HTTP/1.0 404 Not Found)
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  // 상태 줄을 클라이언트에 전송
  Rio_writen(fd, buf, strlen(buf));

  // 응답 본문이 HTML 형식임을 나타내는 Content-type: text/html 헤더를 전송
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // 컨텐츠 길이 헤더 전송:
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // 응답 본문 전송
  Rio_writen(fd, body, strlen(body));

}

// HTTP 요청의 헤더를 읽고 출력하는 역할
void read_requesthdrs(rio_t *rp)  // 요청을 읽기 위한 rio 구조체의 포인터
{
  printf("read_requesthdrs함수호출\n");
  char buf[MAXLINE];    // HTTP 요청 헤더를 읽기 위한 버퍼

  // 첫 번째 요청 헤더 읽기:
  // 요청 헤더의 첫 번째 줄을 buf에 읽어온다. rp는 rio_t 구조체의 포인터로 요청 데이터 읽는데 씀
  Rio_readlineb(rp, buf, MAXLINE);

  // buf와 빈 줄이 아닐 경우, 계쏙 다음 헤더 라인을 읽음
  while(strcmp(buf, "\r\n")) {   //buf 와 \r\n이 같으면 0.
    // 요청 헤더의 다음 줄을 읽고 출력
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

// URI 분석하여 파일 경로와 CGI인자들을 추출
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  printf("parse_uri함수호출\n");
  char *ptr;    // 문자열 검색 결과를 저장할 포인터

  if (!strstr(uri, "cgi-bin")) { //uri가 cgi-bin을 포함하고 있지 않으면 static content
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/')    // URI가 '/' 로 끝나면
      strcat(filename, "home.html");  // finename에 "home.html"을 추가하여 기본 파일을 지정
    return 1;
  }
  else {  /* Dynamic content */
    ptr = index(uri,'?');     // URI에 쿼리 문자열이 있는지 확인후 있으면 prt은 ? 의 위치 가리킴
    if (ptr) {
      strcpy(cgiargs, ptr+1); 
      *ptr = '\0';  // 문자열의 끝으로 바꾸어 쿼리 문자열을 URI에서 제거
                    // 이로 인해 filename이 CGI 프로그램의 경로를 정확하게 가리키게 됨
    }
    else                  // 쿼리 문자열이 없으면 
      strcpy(cgiargs,""); // cgiargs를 빈 문자열로 설정
    strcpy(filename,"."); 
    strcat(filename,uri);
    return 0;
  }
}

// 정적 컨텐츠를 클라에게 제공
void serve_static(int fd, char *filename, int filesize)
{
  printf("serve_static함수호출\n");
  int srcfd;  // 파일의 디스크 상의 식별자
  // srcp: 파일을 메모리에 매핑한 시작 주소
  // filetype: 파일의 MIME 타입을 저장하는 버퍼
  // buf: HTTP 응답 헤더와 바디를 저장하는 버퍼
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  // 파일의 MIME 타입을 결정하여 filetype에 저장
  get_filetype(filename, filetype);
  // sprintf를 사용하여 HTTP 응답 헤더 구성
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  // 파일의 크기를 바이트 단위로 표시
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  // 파일의 MIME 타입을 지정
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  // 생성한 응답 헤더를 클라이언트에게 전송
  Rio_writen(fd, buf, strlen(buf));

  printf("Response headers:\n");
  printf("%s", buf);

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0); // 요청된 파일을 읽기 전용으로 연다 // srcfd는 파일의 디스크 식별자
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //요청한 파일을 가상메모리에 매핑 // srcp는 매핑된 메모리의 시작 주소
  Close(srcfd);  //매핑했으니 이제 식별자 필요없음  
  Rio_writen(fd, srcp, filesize); // 주소 srcp에서 시작하는 filesize byte를 클라이언트의 연결식별자로 복사. (파일 전송)
  Munmap(srcp, filesize); //매핑된 가상메모리 영역을 free 
}

// 파일 이름에 기반하여 적절한 MIME 타입을 결정하고 filetype 변수에 저장하는 역할
// filename: 파일의 이름을 포함하는 문자열
// filetype: 결정된 MIME 타입을 저장할 버퍼
void get_filetype(char *filename, char *filetype)
{
  printf("get_filetype함수호출\n");
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}

// 동적 컨텐츠를 클라이언트에 제공하기 위해 CGI 프로그램을 실행하는 기능 수행
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { //자식 프로세스 fork (자식 프로세스의 context에서 cgi 프로그램을 돌린다)
    setenv("QUERY_STRING", cgiargs, 1);  // QUERY_STRING 환경변수를 CGI 인자로 설정
    Dup2(fd, STDOUT_FILENO); // 자식의 표준 출력을 연결 식별자로 redirect.
    Execve(filename, emptylist, environ); // cgi 프로그램 실행. 
    // Execve가 성공하면, 다음 줄의 코드는 실행되지 않음
  }
  Wait(NULL); // 부모 프로세스는 자식 프로세스가 종료될 때까지 대기
}
