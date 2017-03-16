#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <alloca.h>
#include <string.h>

#include <mpi.h>
#include <unistd.h>

#include <bigfile.h>
#include <bigfile-mpi.h>

int ThisTask;
int NTask;

static void 
info(char * fmt, ...) {

    static double t0 = -1.0;

    MPI_Barrier(MPI_COMM_WORLD);

    if(t0 < 0) t0 = MPI_Wtime();

    char * buf = alloca(strlen(fmt) + 100);
    sprintf(buf, "[%010.3f] %s", MPI_Wtime() - t0, fmt );

    if(ThisTask == 0) {
        va_list va;
        va_start(va, fmt);
        vprintf(buf, va);
        va_end(va);
    }
}

#define MODE_CREATE 0
#define MODE_READ   1
#define MODE_UPDATE 2

typedef struct log {
    double create;
    double open;
    double write;
    double read;
    double close;
} log;

int nmemb = 1;
size_t BytesPerBlobFile = 1024 * 1024 * 1024;
int Nwriter = 0;
int aggregated = 0;
size_t size = 1024;
int mode = MODE_CREATE;
int purge = 0;

static void
iosim(char * filename)
{
    size_t elsize = 8;
    size_t start;
    size_t localsize;
    size_t Nfile;
    
    if(aggregated) {
        /* Use a large enough number to force aggregated IO */
        big_file_mpi_set_aggregated_threshold(size * nmemb * 8);
    } else {
        /* Use 0 to force no aggregated IO */
        big_file_mpi_set_aggregated_threshold(0);
    }
    BigFile bf = {0};
    BigBlock bb = {0};
    BigArray array = {0};
    BigBlockPtr ptr = {0};

    uint64_t * fakedata;
    ptrdiff_t i;
    //
    //+++++++++++++++++ Timelog variables +++++++++++++++++
    double t0, t1;
    log trank;
    trank.create = trank.open = trank.write = trank.read = trank.close = 0;
    int nel_trank = sizeof(trank) / sizeof(trank.create);
    //
    //+++++++++++++++++ END +++++++++++++++++


    if(mode == MODE_CREATE) {
        Nfile = (size * nmemb * elsize + BytesPerBlobFile - 1)/ BytesPerBlobFile;
        info("Creating BigFile\n");
        t0 = MPI_Wtime();
        big_file_mpi_create(&bf, filename, MPI_COMM_WORLD);
        t1 = MPI_Wtime();
        trank.create += t1 - t0;
        info("Created BigFile\n");

        info("Creating BigBlock\n");
        t0 = MPI_Wtime();
        big_file_mpi_create_block(&bf, &bb, "TestBlock", "i8", nmemb, Nfile, size, MPI_COMM_WORLD);
        t1 = MPI_Wtime();
        trank.create += t1 - t0;
        info("Created BigBlock\n");
    }  else {
        info("Opening BigFile\n");
        t0 = MPI_Wtime();
        big_file_mpi_open(&bf, filename, MPI_COMM_WORLD);
        t1 = MPI_Wtime();
        trank.open += t1 - t0;
        info("Opened BigFile\n");

        info("Opening BigBlock\n");
        t0 = MPI_Wtime();
        big_file_mpi_open_block(&bf, &bb, "TestBlock", MPI_COMM_WORLD);
        t1 = MPI_Wtime();
        trank.open += t1 - t0;
        info("Opened BigBlock\n");
        size = bb.size;
        nmemb = bb.nmemb;
        Nfile = bb.Nfile;
    }

    start = size * ThisTask / NTask;
    localsize = size * (ThisTask + 1) / NTask - start;

    info("Writing to `%s`\n", filename);
    info("mode %d\n", mode);
    info("nmemb %d\n", nmemb);
    info("Size %td\n", size);
    info("NBlobFiles %td\n", Nfile);
    info("BytesPerBlob %td\n", Nfile);
    info("Ranks %d\n", NTask);
    info("Writers %d\n", Nwriter);
    info("Aggregated %d\n", aggregated);
    info("LocalBytes %td\n", localsize * elsize * nmemb);
    info("LocalSize %td\n", localsize);

    fakedata = malloc(elsize * localsize * nmemb);
    big_array_init(&array, fakedata, "i8", 2, (size_t[]){localsize, nmemb}, NULL);


    if(mode == MODE_CREATE || mode == MODE_UPDATE) {
        info("Initializing FakeData\n");
        for(i = 0; i < localsize; i ++) {
            int j;
            for(j = 0; j < nmemb; j ++) {
                fakedata[i * nmemb + j] = start + i;
            }
        }
        info("Initialized FakeData\n");
        info("Writing BigBlock\n");
        t0 = MPI_Wtime();
        big_block_mpi_write(&bb, &ptr, &array, Nwriter, MPI_COMM_WORLD);
        t1 = MPI_Wtime();
        trank.write += t1 - t0;
        info("Written BigBlock\n");
        info("Writing took %f seconds\n", trank.write);

    }
    else {
        info("Reading BigBlock\n");

        big_block_seek(&bb, &ptr, 0);
        t0 = MPI_Wtime();
        big_block_mpi_read(&bb, &ptr, &array, Nwriter, MPI_COMM_WORLD);
        t1 = MPI_Wtime();
        trank.read += t1 - t0;
        info("Reading took %f seconds\n", trank.read);
        info("Initializing FakeData\n");
        for(i = 0; i < localsize; i ++) {
            int j;
            for(j = 0; j < nmemb; j ++) {
                //printf("%lX ", fakedata[i * nmemb + j]);
                if (fakedata[i * nmemb + j] != start + i) {
                    info("data is corrupted either due to reading or writing\n");
                    abort();

                }
            }
        }
        //printf("\n");
        info("Initialized FakeData\n");
    }

    info("Closing BigBlock\n");
    t0 = MPI_Wtime();
    big_block_mpi_close(&bb, MPI_COMM_WORLD);
    t1 = MPI_Wtime();
    trank.close += t1 - t0;
    info("Closed BigBlock\n");

    info("Closing BigFile\n");
    t0 = MPI_Wtime();
    big_file_mpi_close(&bf, MPI_COMM_WORLD);
    t1 = MPI_Wtime();
    trank.close += t1 - t0;
    info("Closed BigFile\n");


    free(fakedata);

    log * times = malloc(sizeof(log) * NTask);

    MPI_Gather(&trank, sizeof(trank), MPI_BYTE, times, sizeof(trank), MPI_BYTE, 0, MPI_COMM_WORLD);

    if (ThisTask == 0){
        char timelog[4096];
        sprintf(timelog, "%s/Timelog", filename);
        FILE * fp = fopen(timelog, "a+");
        if (!fp){
            info("iosim.c: Couldn't open file %s for writting!\n", timelog);
        }
        else{
            fprintf(fp, "# mode\tNfile\tbytesperblob\tranks\twriters\titems\tnmemb\n"
                        "%d\t%d\t%td\t%d\t%d\t%td\t%d\n",
                         mode, Nfile, BytesPerBlobFile, NTask, Nwriter, size, nmemb);

            fprintf(fp, "# Task\tTcreate\t\tTopen\t\tTwrite\t\tTread\t\tTclose\n");
            for (i=0; i<NTask; i++) {
                fprintf(fp, "%d\t%f\t%f\t%f\t%f\t%f\n",
                    i, times[i].create, times[i].open, times[i].write, times[i].read, times[i].close);
            }
        }
        fclose(fp);
    }
    free(times);
}

char * getoptstr = "hb:n:s:w:m:Ap";
static void 
usage() 
{
    if(ThisTask != 0) return;
    printf("usage: bigfile-iosim [-%s] command filename \n", getoptstr);

    printf("  command : create / update / read \n"
           " -A : Force Aggreated Mode \n"
           " -b N : set number of bytes per blob file to N\n"
           " -n N : set number of writer subcommunicators to N; 0 for number of MPI ranks\n"
           " -s N : set number of rows in the block to N (for create)\n"
           " -w N : set width / nmemb of a block to N (for create)\n"
           " -p : purge the block afterwards \n "
           );

    printf("Defaults: -b %ld -n %d -s %d -w %d\n", 
            BytesPerBlobFile, Nwriter, size, nmemb);
}


int main(int argc, char * argv[]) {
    MPI_Init(&argc, &argv);
    
    MPI_Comm_rank(MPI_COMM_WORLD, &ThisTask);
    MPI_Comm_size(MPI_COMM_WORLD, &NTask);
    
    int i;
    int ch;
    char * filename = alloca(1500);
    
    while(-1 != (ch = getopt(argc, argv, getoptstr))) {
        switch(ch) {
            case 'A':
                aggregated = 1;
                break;
            case 'p':
                purge = 1;
                break;
            case 'w':
                if(1 != sscanf(optarg, "%d", &nmemb)) {
                    usage();
                    goto byebye;
                }
                break;
            case 'b':
                if(1 != sscanf(optarg, "%td", &BytesPerBlobFile)) {
                    usage();
                    goto byebye;
                }
                break;
            case 'n':
                if(1 != sscanf(optarg, "%d", &Nwriter)) {
                    usage();
                    goto byebye;
                }
                break;
            case 's':
                if(1 != sscanf(optarg, "%td", &size)) {
                    usage();
                    goto byebye;
                }
                break;
            case 'h':
            default:
                usage();
                goto byebye;
        }    
    }
    if(optind == argc) {
        usage();
        goto byebye;
    }

    if(0 == strcmp(argv[optind], "read")) { mode = MODE_READ; } else
    if(0 == strcmp(argv[optind], "create")) { mode = MODE_CREATE; } else
    if(0 == strcmp(argv[optind], "update")) { mode = MODE_UPDATE; }
    else { usage(); goto byebye; }

    optind++;

    sprintf(filename, "%s", argv[optind]);

    if (Nwriter > NTask) {
        info("############## CAUTION: you chose %d ranks and %d writers! ##############\n"
             " #  If you want %d writers, allocate at least %d ranks with <mpirun -n %d> #\n"
             " ################### Can only use %d writers instead! ###################\n",
             NTask, Nwriter, Nwriter, Nwriter, Nwriter, NTask);
        Nwriter = NTask;
    }

    iosim(filename);

    if (purge) {
        if(ThisTask == 0) {
            char buffer[1500];
            sprintf(buffer, "rm -rf %s/TestBlock", filename);
            system(buffer);
        }
    }
//+++++++++++++++++ Writing Time Log +++++++++++++++++
byebye:
    MPI_Finalize();
    return 0;
}
