#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <string.h>
#include <dirent.h>

#define SECOND_TO_MICRO 1000000
#define BUFLEN 1024

struct fileinfo {
	char f_name[BUFLEN];
	time_t m_time;
};

typedef struct fileinfo finfo;

void ssu_runtime(struct timeval *begin_t, struct timeval *end_t); //실행시간을 재는 함수
void ssu_mntr(); //모니터링 프로그램 프롬포트 관련 함수 
void ssu_back_mntr();
void check_info();
void ssu_checkfile (finfo *file_info1, int nitems1, time_t *intertime);
void recover_file(char *fname, char *option);
void print_tree();
int ssu_daemon_init(void);
void delete_mntr(char *filename, char *endday, char *endtime, char *option);
void ssu_scandir(char *dirname);
void delete_directory(char *dirname);
void print_size();
void size_d_option(char* name);
int get_timeval(int year, int month, int day, int hour, int minute);
int daemon_ssu_scandir(char *path, finfo *file_info, int* totalsize);

char checkpath[BUFLEN];
char savepath[BUFLEN];
char trashpath[BUFLEN];
char infopath[BUFLEN];
char filespath[BUFLEN];

struct stat statbuf;

int indent=1;

int size_print_indent;

pid_t deletepid=1;

int d_option_num;
int d_option_depth;

int daemon_scan_num;

int main(void){

	struct timeval begin_t, end_t;
	gettimeofday(&begin_t, NULL);

	ssu_mntr();

	gettimeofday(&end_t, NULL);

	if(deletepid>0)
		ssu_runtime(&begin_t, &end_t);

	exit(0);

}

static inline int sortbydatetime(const struct dirent **a, const struct dirent **b) //scandir() 함수에서 해당 디렉토리내 파일을 수정시간 순으로 정렬할 때 필요 
{
	int rval;
	struct stat sbuf1, sbuf2;
	char path1[BUFLEN], path2[BUFLEN];

	snprintf(path1, BUFLEN, "%s/%s", infopath,  (*a)->d_name);
	snprintf(path2, BUFLEN, "%s/%s", infopath, (*b)->d_name);

	rval = stat(path1, &sbuf1);
	if(rval) {
		perror("stat");
		return 0;
	}
	rval = stat(path2, &sbuf2);
	if(rval) {
		perror("stat");
		return 0;
	}

	return sbuf1.st_mtime < sbuf2.st_mtime;
}

void print_help(){

	printf("/****파일 모니터링 프로그램****/\n1.delete [FILENAME] [DATE] [TIME]\n2.recover [FILENAME]\n3.tree\n4.help");
}


void ssu_mntr(){

	pid_t pid;
	char *word, *p, c;
	char *token[5];

	word=malloc(BUFLEN);

	for(int i=0; i<5; i++){
		token[i]=malloc(BUFLEN);
		token[i]="";
	}

	if(access("check", F_OK)<0) //서브 디렉토리 지정
		mkdir("check",0755);

	memset(checkpath, 0, BUFLEN);
	memset(savepath, 0, BUFLEN);
	memset(trashpath, 0, BUFLEN);
	memset(filespath, 0, BUFLEN);
	memset(infopath, 0, BUFLEN);

	getcwd(savepath, BUFLEN); //디렉토리 주소 저장 -> 서브 디렉토리 지정 

	chdir("check");

	getcwd(checkpath,BUFLEN); //디렉토리 주소 저장 -> check 디렉토리 지정

	chdir(savepath);

	if(access("trash", F_OK)< 0)
		mkdir("trash",0755);

	chdir("trash");

	getcwd(trashpath, BUFLEN); //디렉토리 주소 저장 -> trash 디렉토리 지정

	if(access("files", F_OK) <0)
		mkdir("files",0755);
	if(access("info", F_OK) < 0)
		mkdir("info",0755);

	chdir("info");

	getcwd(infopath, BUFLEN); //디렉토리 주소 저장 -> trash/info 디렉토리 지정

	chdir(trashpath);
	chdir("files");

	getcwd(filespath, BUFLEN); //디렉토리 주소 저장 -> trash/files 디렉토리 지정

	chdir(savepath);

	/*해당 프로그램에 필요한 디렉토리를 모두 절대경로로 저장함*/

	if((pid = fork()) < 0){
		fprintf(stderr, "fork error\n");
		exit(1);
	}
	else if (pid == 0) {
		if(ssu_daemon_init()<0) {
			fprintf(stderr, "ssu_daemon_init failed\n");
			exit(1);
		}
	}
	else {
		while(1){

			memset(word, 0, BUFLEN);
			printf("\n20182633>");
			scanf("%[^\n]", word);
			getchar();
			if(strlen(word)==0)
				continue;
			p=strtok(word," ");
			for(int i=0;p!=NULL;i++){
				token[i]=p;
				p=strtok(NULL," ");
			}
			if(!strcmp(token[0],"exit")){ //exit 기능 구현
				printf("프로그램을 종료합니다!\n");
				return;
			}
			else if(!strcmp(token[0], "delete")){ //delete 기능 구현
				delete_mntr(token[1], token[2], token[3], token[4]);
				check_info(); //info 검사
			}
			else if(!strcmp(token[0],"recover")){ //recover 기능 구현
				recover_file(token[1], token[2]);
			}
			else if(!strcmp(token[0], "tree")){ //tree 기능 구현
				print_tree();
			}
			else if(!strcmp(token[0], "size"))
				print_size(token[1], token[2], token[3]);
			else if(!strcmp(token[0], "help")) //help 기능 구현
				print_help();
			else //다른 명령어를 입력했을 시 help 화면 출력
				print_help();
		}
	}

}

int get_timeval(int year, int month, int day, int hour, int minute){ //입력 받은 시간과 현재 시간의 차이를 구함 -> 이를 통해 몇 초후에 삭제를 할지 정한다. 

	struct tm *t, t_in;
	time_t timer = time(NULL);
	int val, inputmin, nowmin;
	t = localtime(&timer);

	t_in.tm_year = year - 1900;
	t_in.tm_mon = month - 1;
	t_in.tm_mday = day;
	t_in.tm_hour = hour;
	t_in.tm_min = minute;
	t_in.tm_sec = 0;

	mktime(&t_in); //mktime() 함수를 통해 입력 받은 시간으로 tm 구조체를 만든다

	inputmin = (t_in.tm_yday)*1440+(t_in.tm_hour)*60+(t_in.tm_min);
	nowmin = (t->tm_yday)*1440+(t->tm_hour)*60+(t->tm_min); //해당 시간을 분단위로 변환 

	val = inputmin - nowmin; //차이를 구함

	return val*60;
}

void print_tree(){ //tree를 출력하는 함수


	printf("check───────────┐\n");
	ssu_scandir(checkpath); //해당 디렉토리를 스캔하며 출력하는 함수 실행 

}

void size_print_func(off_t *size, char *name){

	char *path;

	path = malloc(BUFLEN);

	if(name[0]=='.') //만약 해당 인자가 '.'으로 시작한다면 상대경로라는 뜻이므로
		printf("%ld\t\t%s\n",(long int)*size, name); //그대로 출력
	else{ //아니라면 파일 이름만 인자로 넣었단 의미므로
		sprintf(path,"./%s",name); //'./'까지 더한 후
		printf("%ld\t\t%s\n",(long int)*size, path); //출력
	}

}

void print_size(char* name, char* option, char *num){

	struct dirent **items;
	struct stat statbuf;
	int  nitems;
	char *path;

	d_option_depth=atoi(num);
	d_option_num=0;
	path=malloc(BUFLEN);

	chdir(savepath);

	if(!strcmp(option,"")){ //옵션 없이 실행했을 경우
		if(stat(name, &statbuf)<0){ //stat함수로 해당 파일의 정보 구조체를 염
			fprintf(stderr, "해당 %s 파일이 없습니다!\n", name); //없다면 오류 리턴
			return;
		}
		else
			size_print_func(&statbuf.st_size, name);
	}
	else{ //옵션 있게 실행했을 경우 
		if(stat(name, &statbuf)<0){ //stat함수로 해당 파일의 정보 구조체를 염
			fprintf(stderr, "해당 %s 파일이 없습니다!\n", name); //없다면 오류 리턴
			return;
		}
		if(statbuf.st_mode & S_IFDIR== S_IFDIR) //해당 파일이 폴더인지 확인 ->폴더라면
		{
			d_option_num++;
			size_print_func(&statbuf.st_size, name);
			size_d_option(name);
		}
		else //파일일 경우
			size_print_func(&statbuf.st_size, name);
	}
}


void size_d_option(char* name)
{
	struct stat statbuf;
	struct dirent **items;
	int nitems;
	char *path;

	path = malloc(BUFLEN);

	chdir(name);

	nitems=scandir(".", &items, NULL, alphasort); //디렉토리 내 파일 리스트 받음 
	for(int j=0; j<nitems; j++){
		if(!strcmp(items[j]->d_name, ".") || !strcmp(items[j]->d_name, ".."))
				continue;
		stat(items[j]->d_name, &statbuf);
		sprintf(path,"%s/%s",name,items[j]->d_name);
		size_print_func(&statbuf.st_size, path);
		if(statbuf.st_mode & S_IFDIR == S_IFDIR){
			d_option_num++;
			if(d_option_num<d_option_depth)
			   size_d_option(path); //재귀함수 통해 해당 디렉토리 내 파일 출력
		}
	}

	chdir("..");

}


void delete_mntr(char *filename, char *endday, char *endtime, char *option) { //삭제 기능 구현 

	struct dirent *dirp, **items;
	int nitems, year=0, month=0, day=0, hour=0, minute=0, val=0;
	DIR *dp;
	FILE *fp;
	struct stat statbuf;
	char *rpath, *tpath, *dupath;
	int dup_num=2;
	char *realfilename;
	char *p;
	char *pathbuf[10];
	int bufnum;
	char *cpy;
	char deletetime[80], modifytime[80];
	char inputchar;
	struct tm *t1, *t2;
	time_t timer;

	chdir(trashpath);

	cpy=malloc(BUFLEN);
	realfilename= malloc(BUFLEN);
	rpath = malloc(BUFLEN);
	tpath = malloc(BUFLEN);
	dupath = malloc(BUFLEN);

	//파일 실제 이름 (경로 제외) 추출하기

	strcpy(cpy, filename);

	if((cpy[0]=='/')||(cpy[0]=='.')){ //filename이 '/'나 ','로 시작한다면?
		p=strtok(cpy,"/");
		for(bufnum=0; p!=NULL; bufnum++){
			pathbuf[bufnum]=p;
			p=strtok(NULL,"/");
		}
		strcpy(realfilename, pathbuf[bufnum-1]); //realfilename -> 실제 파일 이름 
	}
	else
		strcpy(realfilename, cpy);

	chdir(filespath); //중복 거르는 작업 시작

	strcpy(dupath, realfilename);

	while(1) {

		if(access(dupath, F_OK)<0)//files 디렉토리 안에 해당 파일이 있는지 확인
			break;
		else{
			sprintf(dupath,"%d_%s",dup_num, realfilename);
			dup_num++;
		}//있다면 -> 앞에 숫자를 붙임 -> 없을때 까지 반복
	} // dupath->n_파일이름 

	sprintf(tpath, "%s/files/%s", trashpath, dupath); //tpath->실제 trash/files/파일 이름 

	if(strlen(endday)&&strlen(endtime)){ //기다리는 시간 구현
		sscanf(endday, "%d-%d-%d", &year, &month, &day);
		sscanf(endtime, "%d:%d", &hour, &minute);

		val=get_timeval(year, month, day, hour, minute);
		printf("%d초 후 delete 실행\n", val);

		if((deletepid=fork())<0){
			fprintf(stderr, "fork error\n");
			return;
		}
		else if(deletepid==0){ //자식 프로세스에서 삭제 과정 진행 
			sleep(val);
			chdir(checkpath);

			stat(filename, &statbuf); //수정시간, 삭제 시간 받아오기
			time(&timer);
			t1= localtime(&timer);
			strftime(deletetime, 80,"%F %H:%M", t1);
			t2=localtime(&statbuf.st_mtime);
			strftime(modifytime, 80, "%F %H:%M", t2);


			if(filename[0]=='/'){ //절대경로일 경우
				if(rename(filename, tpath)<0){
					fprintf(stderr,"delete 에러!\n");
					return;
				}
			}
			else if(filename[0]=='.'){ //상대경로일 경우
				if(realpath(filename, rpath)==NULL){
					fprintf(stderr, "해당 파일이 없습니다!\n");
					return;
				}	
				if(rename(rpath, tpath)<0){
					fprintf(stderr,"delete 에러!\n");
					return;
				}
			}
			else{ //파일 이름만 줄 경우
				if(realpath(filename, rpath)==NULL){
					fprintf(stderr, "해당 파일이 없습니다!\n");
					return;
				}
				if(rename(rpath, tpath)<0){
					fprintf(stderr, "delete 에러!\n");
					return;
				}
			}

			chdir(infopath);
			fp=fopen(dupath,"w"); //인포디렉터리에 파일 생성 

			fprintf(fp,"[Trash info]\n%s\nD : %s\nM : %s\n",rpath, deletetime, modifytime); //info디렉토리 내에 절대경로, 삭제시간, 최종 수정시간 쓰기 

			fclose(fp);

			printf("delete 완료!\n");

			exit(0);


		}
		else
			return;
	}

	chdir(checkpath);

	stat(filename, &statbuf); //수정시간, 삭제 시간 받아오기
	time(&timer);
	t1= localtime(&timer);
	strftime(deletetime, 80,"%F %H:%M", t1);
	t2=localtime(&statbuf.st_mtime);
	strftime(modifytime, 80, "%F %H:%M", t2);


	if(filename[0]=='/'){ //절대경로일 경우
		if(rename(filename, tpath)<0){
			fprintf(stderr,"delete 에러!\n");
			return;
		}
	}
	else if(filename[0]=='.'){ //상대경로일 경우
		if(realpath(filename, rpath)==NULL){
			fprintf(stderr, "해당 파일이 없습니다!\n");
			return;
		}	
		if(rename(rpath, tpath)<0){
			fprintf(stderr,"delete 에러!\n");
			return;
		}
	}
	else{ //파일 이름만 줄 경우
		if(realpath(filename, rpath)==NULL){
			fprintf(stderr, "해당 파일이 없습니다!\n");
			return;
		}
		if(rename(rpath, tpath)<0){
			fprintf(stderr, "delete 에러!\n");
			return;
		}
	}

	chdir(infopath);
	fp=fopen(dupath,"w"); //인포디렉터리에 파일 생성 

	fprintf(fp,"[Trash info]\n%s\nD : %s\nM : %s\n",rpath, deletetime, modifytime); //info디렉토리 내에 절대경로, 삭제시간, 최종 수정시간 쓰기 

	fclose(fp);

	printf("delete 완료!\n");

	return;

}

void delete_directory(char *dirname){ //디렉토리 내 파일 삭제하는 함수

	struct dirent **items;
	struct stat statbuf;
	int nitems;

	chdir(dirname);

	nitems = scandir(".", &items, NULL, alphasort);

	for(int i=0; i<nitems; i++){
		if(!strcmp(items[i]->d_name, ".")||!strcmp(items[i]->d_name,".."))
			continue;

		stat(items[i]->d_name, &statbuf);
		if((statbuf.st_mode&S_IFDIR)==S_IFDIR) //만약 디렉토리 파일이라면
			delete_directory(items[i]->d_name); //재귀 함수 실행 
		else
			remove(items[i]->d_name);
	}
}

void recover_file(char *filename, char *option){ //파일을 복구하는 함수 

	struct dirent **items;
	struct stat statbuf;
	int nitems, i, j;
	char *buf;
	int num=0;
	char *tfname[7];
	char *trfname;
	FILE *fp;
	char *buf1, *buf2, *buf3, *buf4;
	char *p;
	int inputnum;
	int filenum[30]={0};
	char *infotext[30];
	char *cpath, *fpath;
	int tmp;

	for(int n=0; n<30; n++){
		infotext[n]=malloc(BUFLEN);
	}

	for(int n=0; n<7; n++){
		tfname[n]=malloc(BUFLEN);
	}

	trfname=malloc(BUFLEN);
	buf=malloc(BUFLEN);
	buf1=malloc(BUFLEN);
	buf2=malloc(BUFLEN);
	buf3=malloc(BUFLEN);
	buf4=malloc(BUFLEN);
	cpath=malloc(BUFLEN);
	fpath=malloc(BUFLEN);

	chdir(filespath);

	nitems=scandir(".", &items, NULL, alphasort); //files 폴더 내 파일 디렉토리 검사 

	for(i=0; i<nitems; i++){

		chdir(filespath);

		if(!strcmp(items[i]->d_name, ".")||!strcmp(items[i]->d_name, ".."))
			continue;
		if(!strcmp(items[i]->d_name, filename)){ //만약 files 폴더 내 파일 이름과 입력받은 이름이 같다면 
			num++;
			chdir(infopath);
			fp=fopen(items[i]->d_name,"r"); //info 폴더 내 해당 파일을 열어
			fscanf(fp,"%[^\n]\n%[^\n]\n%[^\n]\n%[^\n]\n", buf1, buf2, buf3, buf4); //정보를 받아온 후
			sprintf(infotext[num], "%d. %s %s %s\n", num, items[i]->d_name ,buf3 ,buf4 );//버퍼에 저장
			filenum[num]=i; //filenum 배열에 해당 정보를 저장
			fclose(fp); //파일 닫기
			continue;
		}

		strcpy(buf, items[i]->d_name);

		p=strtok(buf, "_"); //중복된 파일이 있을 수 있으므로 "_"를 기준으로 분리
		for(j=0; p!=NULL; j++){
			tfname[j]=p;
			p=strtok(NULL,"_");
		}
		strcpy(trfname,tfname[j-1]); //실제 파일명 추출

		if(!strcmp(trfname, filename)){ //실제 파일명과 입력 받은 이름이 같다면 
			num++;
			chdir(infopath);
			fp=fopen(items[i]->d_name,"r"); //info 폴더 내 해당 파일을 열어
			fscanf(fp,"%[^\n]\n%[^\n]\n%[^\n]\n%[^\n]\n", buf1, buf2, buf3, buf4); //정보를 받아온 후
			sprintf(infotext[num], "%d. %s %s %s\n", num, trfname ,buf3 ,buf4);//버퍼에 저장
			filenum[num]=i;
			fclose(fp);
		}
	}

	if(num==0){ //파일 없다 출력
		printf("There is no '%s' in the 'trash' directory\n", filename);
	}
	else if(num==1){ //똑같은 이름이 하나 있을때
		sprintf(fpath,"%s/%s",filespath,items[filenum[1]]->d_name);
		sprintf(cpath,"%s/%s",checkpath,items[filenum[1]]->d_name);
		rename(fpath,cpath); //rename으로 경로 바꿔줌 
		chdir(infopath); //info 폴더에 가서
		remove(items[filenum[1]]->d_name); //파일 삭제
		printf("복구 완료\n");
	}
	else if(num>1){ //똑같은 이름이 여러개 있을때
		tmp=num;
		for(num=1; num<=tmp; num++) //여러 파일의 정보 출력
		{
			printf("%s\n", infotext[num]);
		}
		printf("Choose:"); //선택 받은 후
		scanf("%d", &inputnum);
		printf("%s/%s\n", filespath, items[filenum[inputnum]]->d_name);
		sprintf(fpath,"%s/%s",filespath,items[filenum[inputnum]]->d_name);
		sprintf(cpath,"%s/%s",checkpath,items[filenum[inputnum]]->d_name);
		rename(fpath,cpath); //rename으로 경로 바꿔줌
		chdir(infopath);//info 폴더에 간 후
		remove(items[filenum[inputnum]]->d_name); //파일 삭
		printf("복구 완료!\n");
	}

	//trash 디렉토리 읽기
	//파일 이름 받아옴
	//filename 이랑 같은지 확인
	//파일이름 숫자_ 뒤로 자른후 다시 filename이랑 같은지 확인
	//같다면 ?

	//같은게 여러개일 경우
	//출력을 해줘야 하는데... 
	//info 내 문장을 읽어서 출력해주기 

	//같은게 하나일 경우
	//다시 rename 해준 후 info 삭제 

	//같은게 없다면? _
}

void ssu_scandir(char *dirname){ //디렉토리의 트리를 출력하기 위해 해당 디렉토리를 스캔하는 재귀함수 

	struct dirent **items;
	struct stat statbuf;
	int nitems, i, j;
	char *buf;


	chdir(dirname);

	nitems = scandir(".", &items, NULL, alphasort);

	buf=malloc(BUFLEN);

	for(i=0; i<nitems; i++)
	{

		if(!strcmp(items[i]->d_name, ".")||!strcmp(items[i]->d_name, ".."))
			continue;

		stat(items[i]->d_name, &statbuf);

		for(j=0; j<indent; j++){
			if(j==0)
				printf("\t\t");
			else
				printf("│\t\t");
		}

		if((statbuf.st_mode & S_IFDIR) == S_IFDIR) //만약 디렉토리 파일이라면?
		{
			indent++;
			printf("├%s────────────┐\n",items[i]->d_name);
			ssu_scandir(items[i]->d_name);
		}
		else if(i==nitems-1)
			printf("└%s\n", items[i]->d_name);
		else{
			printf("├%s\n", items[i]->d_name);
		}
	}

	indent--;
	chdir("..");
}

void check_info(){ //info 폴더의 정보를 check 하는 함수 

	struct dirent **items;
	struct stat statbuf, statbuf2;
	int nitems, i;
	char *buf;
	long int total_size=0;
	char *filesbuf;

	filesbuf=malloc(BUFLEN);

	chdir(infopath);

	nitems=scandir(".", &items, NULL, sortbydatetime);
	//info 폴더 내 파일을 sortbydatetime 함수를 통해 정렬하여 list를 만듦 

	for(i=0; i<nitems; i++)
	{

		if(!strcmp(items[i]->d_name, ".")||!strcmp(items[i]->d_name, ".."))
			continue;
		stat(items[i]->d_name, &statbuf);
		total_size+=statbuf.st_size; 

	} //size를 계속 더함으로써 total size를 구함

	printf("\ninfo 디렉토리 total size : %ldB\n", total_size);

	if(total_size > 2000) { //total_size가 2KB가 넘는다면? 삭제기능 구현

		printf("!!total size가 2KB를 넘으므로 삭제를 실행합니다.!!\n");

		for(i=nitems-1; i>0; i--){

			if(!strcmp(items[i]->d_name, ".")||!strcmp(items[i]->d_name, ".."))
				continue;

			stat(items[i]->d_name, &statbuf); //제일 오래된 파일의 정보를 받아옴

			printf("삭제할 파일 : %s 파일 사이즈 : %ld\n", items[i]->d_name, (long int)statbuf.st_size);

			//출력

			remove(items[i]->d_name); //info 파일을 삭제
			sprintf(filesbuf,"%s/%s", filespath,items[i]->d_name); //파일을 files 폴더 내의 주소로 바꿈
			stat(filesbuf, &statbuf2); //정보를 얻음
			if((statbuf2.st_mode&S_IFDIR)==S_IFDIR) //만약 디렉토리라면
				delete_directory(filesbuf); //디렉토리를 지우는 함수 실행 (안의 내용물을 지움)
			remove(filesbuf); //빈 폴더 or 파일을  삭제 

			total_size-=statbuf.st_size; //total size 재확인

			printf("제일 오래된 파일 삭제 후 info 디렉토리 size : %ldB\n", total_size); //출력 

			if(total_size < 2000) //totalsize가 2000보다 작을때 까지 계속 함 
				break;

		}
	}


}


void ssu_checkfile(finfo *file_info1, int nitems1, time_t *intertime){

	FILE *fp;
	char tmp[BUFLEN];
	char timebuf[80];
	struct tm *t;
	struct dirent **items2;
	int nitems2, totalsize_2;
	time_t timer;
	struct stat statbuf2;
	finfo file_info2[BUFLEN]={"",0};


	chdir(savepath);

	fp = fopen("log.txt", "a+"); //log.txt 파일을 이어쓰기 형식으로 염

	chdir(checkpath);

	daemon_scan_num=0; //전역 변수 재설정

	nitems2=daemon_ssu_scandir(checkpath, file_info2, &totalsize_2); //해당 디렉토리 내 파일을 구조체에 저장

	//비교 시작

	if(nitems1>nitems2)
	{ //삭제됐을 경우
		for (int i=0;i<nitems1;i++)
		{		
			if(strcmp(file_info1[i].f_name, file_info2[i].f_name))
			{ //두 파일의 이름이 다르다면?
				time(&timer);
				t=localtime(&timer);
				strftime(timebuf, 80, "%F %H:%M:%S", t);
				fprintf(fp, "[%s][delete_%s]\n", timebuf, file_info1[i].f_name); //로그 출력
				break;
			}
		}
		
	}
	else if(nitems1==nitems2){ //수정됐을 경우
		for(int i=0; i<nitems2; i++){
			stat(file_info2[i].f_name, &statbuf2);
			if((file_info2[i].m_time!=file_info1[i].m_time)&&strlen(file_info2[i].f_name)) { //두 파일의 수정 시간이 다르다면?
				t=localtime(&file_info2[i].m_time);
				strftime(timebuf, 80, "%F %H:%M:%S", t);
				fprintf(fp, "[%s][modify_%s]\n", timebuf, file_info2[i].f_name); //로그 출력
				break;
			}
		}
	}
	else if(nitems1<nitems2){ //생성됐을 경우
		for(int i=0; i<nitems2; i++){
			if(strcmp(file_info1[i].f_name, file_info2[i].f_name)){ //두 파일의 이름이 다르다면?
				stat(file_info2[i].f_name, &statbuf2);
				t=localtime(&file_info2[i].m_time);
				strftime(timebuf, 80, "%F %H:%M:%S", t);
				fprintf(fp, "[%s][create_%s]\n", timebuf, file_info2[i].f_name); //로그 출
				break;
			}
		}
	}

	fclose(fp);

	//삭제됐을 경우 -> 전에는 있는데 후에는 없는 파일 찾아야됨
	// 리스트 받기
	//ntiems 갯수 비교후 후가 많다면?
	//순서대로 있는지 확인 비교 후 다르다 하면 전 파일 이름 출력

	//수정됐을 경우 -> mtime 비교
	//scandir로 리스트 받은 후
	//전 mtime과 후 mtime 비교 후 mtime이 다르다면 출력함 

	//생성됐을 경우 -> 전에는 없는데 후에는 있는 파일 찾아야됨
	//scandir로 리스트 받기
	//순서대로 있는지 확인 비교 후 다르다 하면 후 파일 이름 출력

}


int daemon_ssu_scandir(char *path, finfo *file_info, int* totalsize){
	//구조체에 디렉토리 정보를 저장하는 함수

    struct dirent **items;
    struct stat statbuf;
    int nitems, i, j;
    char *buf;

    chdir(path);

    nitems=scandir(".", &items, NULL, alphasort); //scandir로 해당 디렉토리 파일 리스트 받음 

    for(i=0; i<nitems; i++)
    {   
        if(!strcmp(items[i]->d_name, ".")||!strcmp(items[i]->d_name, "..")||(items[i]->d_name)[0]=='.')
            continue;

        stat(items[i]->d_name, &statbuf); //stat() 함수를 통해 해당 파일 정보를 받음

        if(S_ISDIR(statbuf.st_mode)) //만약 디렉토리 파일이라면 
        {
            daemon_ssu_scandir(items[i]->d_name, file_info, totalsize); //재귀함수를 통해 서브 디렉토리 내 파일까지 리스트에 집어넣기 
            continue;
        }

        strcpy(file_info[daemon_scan_num].f_name, items[i]->d_name); //file_info 구조체에 파일 이름 집어넣기
        file_info[daemon_scan_num].m_time=statbuf.st_mtime; //file_info 구조체에 파일 수정 시간 집어넣기
        *totalsize+=statbuf.st_size; //전체 사이즈 넣기 
        daemon_scan_num++; //전역변수 1 추가 

    }   

    chdir("..");

    return daemon_scan_num;
}


int ssu_daemon_init(void) {
	pid_t pid;
	int fd, maxfd;
	struct dirent *dirp;
	struct dirent **items1;
	struct stat statbuf1, statbuf2;
	int nitems1, nitems2;
	time_t intertime;
	DIR *dp;
	char *filename;
	FILE *fp;
	int totalsize_1=0, totalsize_2=0;
	finfo file_info1[BUFLEN]={"",0}, file_info2[BUFLEN]={"",0};

	/**디몬 프로세스 생성 시작**/

	if((pid = fork()) < 0) {
		fprintf(stderr, "fork error\n");
		exit(1);
	}
	else if (pid != 0)
		exit(0);

	pid = getpid();

	printf("process id : %d\n", pid);

	setsid();

	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	maxfd = getdtablesize();

	/**디몬 프로세스 생성 완료**/

	chdir(savepath);
    
	while(1){

		chdir(checkpath);

        daemon_scan_num=0; //전역변수 초기화 

        nitems1 = daemon_ssu_scandir(checkpath, file_info1, &totalsize_1); //해당 경로 내 파일 구조체에 넣기

        sleep(1);

        daemon_scan_num=0; //전역변수 초기화

        nitems2 = daemon_ssu_scandir(checkpath, file_info2, &totalsize_2); //다시 해당 경로 내 파일 구조체에 넣기

    
        if(totalsize_1 != totalsize_2) //만약 둘의 사이즈가 다르다면
            ssu_checkfile(file_info1, nitems1, &statbuf1.st_mtime); //ssu_checkfile() 함수를 실행해 로그 출력 

    } 

	return 0;
}

void ssu_runtime(struct timeval *begin_t, struct timeval *end_t)
{

	end_t->tv_sec -= begin_t->tv_sec;

	if(end_t->tv_usec < begin_t->tv_usec) {
		end_t->tv_sec--;
		end_t->tv_usec+=SECOND_TO_MICRO;
	}

	end_t->tv_usec -= begin_t->tv_usec;
	printf("\nRuntime: %ld:%06ld(sec:usec)\n", end_t->tv_sec, end_t->tv_usec);
}

