//
// Created by gurth on 3/11/21.
//

#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include <string.h>
#include<unistd.h>
#include<error.h>
#include <time.h>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include "bed.h"

using namespace std;
using namespace bed;
using namespace indicators;

bool cmpChrList(BlockListNode* pa, BlockListNode* pb)
{
    return pa->base < pb->base;
}

bool cmpProfileList(ProfileNode* pa, ProfileNode* pb)
{
    if(pa->chr==pb->chr)
        return pa->Start < pb->Start;
    return pa->chr < pb->chr;
}

BED* BED::pThis = nullptr;

void BED::dichotomySearchChr(char* m_beg, char* m_end)
{
    int chrBeg=atoi(m_beg+3);
    int chrEnd=atoi(m_end+3);

    if(chrBeg!=chrEnd)
    {
        char* p=(char*)(size_t)(((size_t)m_beg + (size_t)m_end) >> 1);

        for(int j=0; j<SEARCH_RANGE;j++, p--)
            if(*(p)=='\n') break;
        p++;

        if(p>m_beg)  // if p == m_beg, dichotomySearch(p,m_end) will forever unchanged
        {
            dichotomySearchChr(m_beg, p);
            dichotomySearchChr(p,m_end);
        }
        else if(p==m_beg)
        {
            for(int j=0; j<SEARCH_RANGE;j++, p++)
                if(*(p)=='\n') break;
            p++;
            if(p < m_end)
            {
                dichotomySearchChr(m_beg, p);
                dichotomySearchChr(p,m_end);
            }
            else if(p == m_end)
                dichotomySearchChr(p,m_end);
        }
    }

    if(pThis->chrList[chrBeg].base > m_beg - pThis->mapped)
        pThis->chrList[chrBeg].base = m_beg - pThis->mapped;
}

void BED::dichotomySearchOffset(char *m_beg, char *m_end, char*& ppos, unsigned long pos, bool isBeg)
{
    int pos_beg=atoi(goFrontItem(m_beg, 2));
    int pos_end=atoi(goFrontItem(m_end, 1));
    char* p=(char*)(size_t)(((size_t)m_beg + (size_t)m_end) >> 1);
    for(int j=0; j<SEARCH_RANGE;j++, p--)
        if(*(p)=='\n') break;
    p++;
    int pos_mid_beg=atoi(goFrontItem(p, 1));
    int pos_mid_end=atoi(goFrontItem(p, 2));
    if(p>m_beg)
    {
        if(pos_beg <= pos && pos <= pos_mid_beg)
            dichotomySearchOffset(m_beg, p, ppos, pos, isBeg);
        if(pos_mid_end <= pos && pos <= pos_end)
            dichotomySearchOffset(p, m_end, ppos, pos, isBeg);
    }
    else if(p==m_beg)
    {
        for(int j=0; j<SEARCH_RANGE;j++, p++)
            if(*(p)=='\n') break;
        p++;
        if(p < m_end)
        {
            pos_mid_beg=atoi(goFrontItem(p, 1));
            pos_mid_end=atoi(goFrontItem(p, 2));
            if(pos_beg <= pos && pos <= pos_mid_beg)
                dichotomySearchOffset(m_beg, p, ppos, pos, isBeg);
            if(pos_mid_end <= pos && pos <= pos_end)
                dichotomySearchOffset(p, m_end, ppos, pos, isBeg);
        }
        else if(p == m_end)
        {
            if(isBeg)
                ppos = m_end;
            else
                ppos = m_beg;
        }
    }
}

void BED::methyMining(ProfileNode *& pGene)
{
    // We can think about using a queue to shrink search range
    char* m_beg=pThis->mapped+pThis->chrList[pGene->chr].base;
    char* m_end=m_beg+pThis->chrList[pGene->chr].length;
    char* pGbeg= nullptr, *pGend= nullptr;
    char* p= nullptr;

    m_end--;
    while(true){if((*m_end)!='\n') break; m_end--;}
    for(int j=0; j<SEARCH_RANGE;j++, m_end--)
        if(*(m_end)=='\n') break;
    m_end++;

    dichotomySearchOffset(m_beg,m_end,pGbeg,pGene->Start,true);
    dichotomySearchOffset(m_beg,m_end,pGend,pGene->End,false);

    size_t depth = 0, mCdep = 0;
    p=pGbeg;
    while(true)
    {
        if(p>pGend) break;
        p=goFrontItem(p,3);
        if((*p == '+') & (pGene->chain))
        {
            p=goFrontItem(p, 2);
            depth+=atoi(p);
            p=goFrontItem(p,1);
            mCdep+=atoi(p);
        }
        for(int j=0;j<SEARCH_RANGE;j++, p++)
            if((*p)=='\n') break;
        p++;
    }
    if(depth!=0)
        pGene->methy_ratio=(double)((double)mCdep / (double)depth);
}

void *BED::pthFuncRaw(void *args)
{
    size_t i = pThis->threadNum++;
    size_t begOffset = pThis->base_offset + i*BLOCK_SIZE;

#ifdef _FLAG_TEST
    if(pThis->sb.st_size - begOffset >= BLOCK_SIZE)
    {
        memset(pThis->mapped + begOffset, TEST_CHAR, BLOCK_SIZE);
        pThis->sum=begOffset+BLOCK_SIZE;
    #ifdef SHOW_ALL_INFO
        printf("Base: 0x%016lx Size: 0x%016lx\n",begOffset, BLOCK_SIZE);
    #endif //!SHOW_ALL_INFO
    }
    else
    {
        memset(pThis->mapped + begOffset, TEST_CHAR,pThis->sb.st_size - begOffset);
        pThis->sum=pThis->sb.st_size;
    #ifdef SHOW_ALL_INFO
        printf("Base: 0x%016lx (Last block)\n",begOffset);
    #endif //!SHOW_ALL_INFO
    }
#endif //!_FLAG_TEST

#ifdef SHOW_PROGRESSBAR
    char buff[0x30];
    sprintf(buff,"Processed %ld / %ld",pThis->sum,pThis->sb.st_size);
    pThis->progress+=pThis->progUnit;
    pThis->bar.set_option(indicators::option::PostfixText{buff});
    pThis->bar.set_progress(pThis->progress);
#endif //!SHOW_PROGRESSBAR
    pthread_exit(nullptr);
}

void *BED::pthFuncTag(void *args)
{
    size_t k= pThis->nodeNum++;
    size_t begOffset = pThis->blockList[k].base;
    char* q=&(pThis->mapped[begOffset]);
    char* p=&(pThis->mapped[begOffset+pThis->blockList[k].length -1]);

    for(int j=0; j<SEARCH_RANGE;j++, p--)
        if(*(p)=='\n') break;
    p++;

    dichotomySearchChr(q, p);

    pThis->progress+=pThis->progUnit;
    pThis->bar.set_progress(pThis->progress);
    pthread_exit(nullptr);
}

void *BED::pthFuncBlockList(void *args)
{
    size_t i = pThis->threadNum++;
    size_t k= pThis->nodeNum++;
    size_t begOffset = pThis->base_offset + i*BLOCK_SIZE;
    int j=0;

    if(k)
    {
        for (; j < SEARCH_RANGE; j++)
            if (pThis->mapped[begOffset + j] == '\n') break;
        begOffset = begOffset + j + 1;
    }
    pThis->blockList[k].base=begOffset;

    pThis->progress+=pThis->progUnit;
    pThis->bar.set_progress(pThis->progress);

    pthread_exit(nullptr);
}

void *BED::pthFuncProfile(void *args)
{
    size_t i = pThis->threadNum++;
    size_t begOffset = pThis->base_offset + i*PROC_GENE_SIZE;
    size_t length = PROC_GENE_SIZE;
    if(pThis->geneNum - begOffset < PROC_GENE_SIZE)
        length = pThis->geneNum - begOffset;

    for(size_t j=0;j<length;j++)
    {
        methyMining(pThis->profileList[begOffset+j]);
        pThis->progress+=pThis->progUnit;
        pThis->bar.set_progress(pThis->progress);
    }

    pthread_exit(nullptr);
}

void *BED::pthFuncProfileList(void *args)
{
    size_t i = pThis->threadNum++;
    size_t begOffset = pThis->base_offset + i*BLOCK_SIZE_INDEX;
    size_t length = BLOCK_SIZE_INDEX;
    if(pThis->sbIndex.st_size - begOffset < BLOCK_SIZE_INDEX)
        length=pThis->sbIndex.st_size - begOffset;

    char* p=pThis->mappedIndex+begOffset;
    char* pend=p+length;

    for(; p<pend; p++)
    {
        if((*p) == '#')   // ### tag a item
        {
            if(*(p+1) == '#' && *(p+2) == '#')
            {
                p=p+3;
                char* q=p+SEARCH_RANGE_INDEX;
                for(; p<q; p++)
                {
                    if((*p)=='I')          // Find "ID=gene:" which means a gene
                    {
                        if(memcmp(p, "ID=gene:", 8) == 0)
                        {
                            char* pgoback= p;
                            p+=8;
                            for(int j=0;j<SEARCH_RANGE;j++, pgoback--)
                                if((*pgoback)=='\n') break;
                            pgoback++;

                            auto* pfNtmp=(ProfileNode*) malloc(sizeof(ProfileNode));
                            setValuePfNode(pgoback,p,pfNtmp);
                            pthread_mutex_lock(&pThis->mutex);
                            pThis->profileList[pThis->geneNum ++]=pfNtmp;
                            pthread_mutex_unlock(&pThis->mutex);
                            break;
                        }
                    }//!if((*p)=='I')
                }//! for
            }//! if(*(p+1) == '#' && *(p+2) == '#')
        } //! if((*p) == '#')
    }//! for

    pThis->progress+=pThis->progUnit;
    pThis->bar.set_progress(pThis->progress);

    pthread_exit(nullptr);
}

void BED::setValuePfNode(char *&pgoback, char *&pID, ProfileNode *&pPfN)
{
    pPfN->chr=atoi(pgoback);
    pgoback=goFrontItem(pgoback, 3);
    if(!pgoback) exit(64);
    pPfN->Start=atoll(pgoback);
    pgoback=goFrontItem(pgoback, 1);
    if(!pgoback) exit(64);
    pPfN->End=atoll(pgoback);
    pgoback=goFrontItem(pgoback, 2);
    if(!pgoback) exit(64);
    pPfN->chain=((*pgoback)=='+');
    pgoback=pID;
    for(int j=0;j<SEARCH_RANGE;j++, pID++)
        if((*pID)==';') break;
    memcpy(pPfN->ID, pgoback, (pID-pgoback));
    pPfN->ID[(pID-pgoback)]='\0';
}

char *BED::goFrontItem(char *p, int n)
{
    int i=0;
    for(int j=0;j<SEARCH_RANGE;j++, p++)
    {
        if((*p) == 0x9 || (*p) == ' ')
        {
            for(int k=0;k<SEARCH_RANGE;k++, p++)
                if((*p) != 0x9 && (*p) != ' ') break;
            p--;
            i++;
            if(i>=n) return p+1;
        }
    }
    return nullptr;
}


BED::BED()
{
    pThis= this;
}

BED::BED(char* bedfile)
{
    pThis= this;
    for(auto & i : chrList)
        i={(unsigned long)-1,0};
    strcpy(bedname,bedfile);
    bedfileOpen(bedfile);
}

void BED::bedfileOpen(char *& bedfile)
{
    // Open file
    if((fileHandle = open(bedfile, O_RDWR)) < 0)
    {
        perror("open()") ;
        exit(1);
    }

    // Get file stat
    if((fstat(fileHandle, &sb)) == -1 )
    {
        perror("fstat()") ;
        exit(1);
    }

    // Map file in memory
    mapped = (char*)mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fileHandle, 0);
    if(mapped == (char*)-1)
    {
        perror("mmap") ;
        exit(1);
    }
}

void BED::bedfileClose()
{
    munmap(mapped,sb.st_size);
    close(fileHandle);
}

BED::~BED()
{
    bedfileClose();
}

void BED::process(char *outputfile, Method m)
{
    process(nullptr, outputfile, m);
}

void BED::process(char *gff3file, char *outputfile, Method m)
{
    clock_t t;
    t=clock();

    switch (m)
    {
        case Method::raw:
            processRaw();
            break;
        case Method::tag:
            processTag();
            break;
        case Method::profile:
            if(!outputfile)
                outputfile="methy profile.txt";
            processProfile(gff3file);
            saveProfile(outputfile);
            break;
        default:
            break;
    }

    printf("\n");
    t = clock() - t;
    printf("Total cost: %lf ms\n", ((float)t) / CLOCKS_PER_SEC * 1000);
    // Show cursor
    show_console_cursor(true);
}

void BED::processRaw()
{
    size_t readNum=0;
    size_t restSize=0;
    size_t blockNum=0;
    pthread_t pth[MAXTHREAD];
    char buff[0x30];

    printf("\033[33m[Warning]\033[0m: Start reading ...\n");

    readNum=sb.st_size / BLOCK_READ;
    restSize=sb.st_size % BLOCK_READ;
    progUnit=(double)(100.0 / (sb.st_size / BLOCK_SIZE +1));
    sprintf(buff,"Processed 0 / %ld",sb.st_size);
    bar.set_option(indicators::option::PostfixText{buff});
    bar.set_progress(0);

    base_offset=0;

    for(int j=0; j<=readNum; j++, base_offset+=BLOCK_READ)
    {
        threadNum = 0;
        if(j==readNum)
            blockNum=(restSize / BLOCK_SIZE) +1;
        else
            blockNum=MAXTHREAD;

        // Create thread
        for (int i = 0; i < blockNum; i++)
            pthread_create(&pth[i], nullptr, pthFuncRaw, nullptr);

        // Join thread
        for (int i = 0; i < blockNum; i++)
            pthread_join(pth[i], nullptr);
    }

}

void BED::processTag()
{
    size_t readNum=0;
    size_t restSize=0;
    size_t blockNum=0;
    pthread_t pth[MAXTHREAD];
    char buff[0x30];
    char* p= nullptr;

    printf("\033[33m[Warning]\033[0m: Start tagging ...\n");
    sprintf(buff,"Building block list");
    bar.set_option(indicators::option::PostfixText{buff});
    bar.set_progress(0);

    readNum=sb.st_size / BLOCK_READ;
    restSize=sb.st_size % BLOCK_READ;
    blockList.resize(sb.st_size / BLOCK_SIZE +1);

    base_offset=0;
    nodeNum = 0;
    progUnit=(double)(25.0 / blockList.size());

    for(int j=0; j<=readNum; j++, base_offset+=BLOCK_READ)
    {
        threadNum = 0;
        if(j==readNum)
            blockNum=(restSize / BLOCK_SIZE) +1;
        else
            blockNum=MAXTHREAD;

        // Create thread
        for (int i = 0; i < blockNum; i++)
            pthread_create(&pth[i], nullptr, pthFuncBlockList, nullptr);

        // Join thread
        for (int i = 0; i < blockNum; i++)
            pthread_join(pth[i], nullptr);

    }

    p=mapped;
    while (true)
    {
        if(*(p+3) >= '0' && *(p+3) <= '9') break;
        for(int j=0; j<SEARCH_RANGE;j++, p++)
            if(*(p)=='\n') break;
        p++;
    }
    blockList[0].base=p-mapped;

    for(size_t j=0;j<blockList.size() - 1;j++)
        blockList[j].length=blockList[j+1].base-blockList[j].base;

    p=mapped + sb.st_size - 1;
    if(*(p)=='\n')
        for(int j=0; j<SEARCH_RANGE;j++, p--)
            if(*(p)!='\n') break;
    blockList[blockList.size()-1].length=p - mapped - blockList[blockList.size() - 1].base;

    sprintf(buff,"Tagging");
    bar.set_option(indicators::option::PostfixText{buff});
    pThis->bar.set_progress(25.0);

    nodeNum = 0;
    base_offset=0;
    progUnit=(double)((100.0 - progress) / blockList.size());

    for(int j=0; j<=readNum; j++, base_offset+=BLOCK_READ)
    {
        if(j==readNum)
            blockNum=(restSize / BLOCK_SIZE) +1;
        else
            blockNum=MAXTHREAD;

        // Create thread
        for (int i = 0; i < blockNum; i++)
            pthread_create(&pth[i], nullptr, pthFuncTag, nullptr);

        // Join thread
        for (int i = 0; i < blockNum; i++)
            pthread_join(pth[i], nullptr);
    }

    vector<BlockListNode* >pTmpList;
    for(int i=1;i<128;i++)
        if(chrList[i].base + 1)
            pTmpList.push_back(&(chrList[i]));
    sort(pTmpList.begin(),pTmpList.end(),cmpChrList);
    for(int i=0;i<pTmpList.size()-1;i++)
        pTmpList[i]->length=pTmpList[i+1]->base-pTmpList[i]->base;
    pTmpList[pTmpList.size()-1]->length=sb.st_size-pTmpList[pTmpList.size()-1]->base;
}

void BED::savechrList()
{
    char buff[NAME_MAX];
    strcpy(buff,bedname);
    strcat(buff,".tag");
    ofstream out(buff,ios::out);
    if(!out.is_open())
    {
        perror("ofstream: ") ;
        exit(2);
    }
    out << "# chr base  length" << endl;
    for(int i=1;i<128;i++)
    {
        if(chrList[i].base + 1)
            out << i << " " << chrList[i].base << " " << chrList[i].length << endl;
    }
    out.close();
}

void BED::processProfile(char *&gff3file)
{
    if((indexHandle = open(gff3file, O_RDWR)) < 0)
    {
        perror("gff3 open()") ;
        exit(2);
    }

    if((fstat(indexHandle, &sbIndex)) == -1 )
    {
        perror("gff3 fstat()") ;
        exit(2);
    }

    mappedIndex = (char*)mmap(nullptr, sbIndex.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, indexHandle, 0);
    if(mappedIndex == (char*)-1)
    {
        perror("gff3 mmap") ;
        exit(2);
    }

    if(pthread_mutex_init(&mutex,NULL)!=0)
    {
        perror("pthread_mutex_init:");
        exit(2);
    }
    // Build up profile list

    size_t readNum=0;
    size_t restSize=0;
    size_t blockNum=0;
    pthread_t pth[MAXTHREAD];
    char buff[0x30];

    printf("\033[33m[Warning]\033[0m: Start generating profile ...\n");

    sprintf(buff,"Indexing");
    bar.set_option(indicators::option::PostfixText{buff});
    pThis->bar.set_progress(0);
    progress=0;

    readNum=sbIndex.st_size / BLOCK_READ_INDEX;
    restSize=sbIndex.st_size % BLOCK_READ_INDEX;

    progUnit=(double)(25.0 / (sbIndex.st_size / BLOCK_SIZE_INDEX +1));

    base_offset=0;

    for(int j=0; j<=readNum; j++, base_offset+=BLOCK_READ_INDEX)
    {
        threadNum = 0;
        if(j==readNum)
            blockNum=(restSize / BLOCK_SIZE_INDEX) + 1;
        else
            blockNum=MAXTHREAD;

        // Create thread
        for (int i = 0; i < blockNum; i++)
            pthread_create(&pth[i], nullptr, pthFuncProfileList, nullptr);

        // Join thread
        for (int i = 0; i < blockNum; i++)
            pthread_join(pth[i], nullptr);
    }
    // Profile generation

    sort(profileList, profileList+geneNum, cmpProfileList);
    base_offset=0;
    readNum=geneNum / PROC_GENE_READ;
    restSize=geneNum % PROC_GENE_READ;

    sprintf(buff,"Processing");
    bar.set_option(indicators::option::PostfixText{buff});

    progUnit=(double)((100.0 - progress) / geneNum);

    for(int j=0; j<=readNum; j++, base_offset+=PROC_GENE_READ)
    {
        threadNum = 0;
        if(j==readNum)
            blockNum=(restSize / PROC_GENE_SIZE) + 1;
        else
            blockNum=MAXTHREAD;

        // Create thread
        for (int i = 0; i < blockNum; i++)
            pthread_create(&pth[i], nullptr, pthFuncProfile, nullptr);

        // Join thread
        for (int i = 0; i < blockNum; i++)
            pthread_join(pth[i], nullptr);
    }

    pthread_mutex_destroy(&mutex);
    munmap(mappedIndex,sbIndex.st_size);
    close(indexHandle);
}

void BED::saveProfile(char *nameProfile)
{
    FILE* fout=fopen(nameProfile,"w");
    if(fout== nullptr)
    {
        perror("fopen(): ");
        exit(2);
    }
    fprintf(fout,"chr\tID\tStart\tEnd\tStrand\tMethy_ratio\n");
    for(int i=0;i<geneNum;i++)
    {
        if(abs(profileList[i]->methy_ratio) <= 1e-15) continue;
        fprintf(fout, "%d\t%s\t%ld\t%ld\t%c\t%.15lf\n", profileList[i]->chr, profileList[i]->ID,
                profileList[i]->Start, profileList[i]->End, (profileList[i]->chain) ? '+' : '-',
                profileList[i]->methy_ratio);
    }
    fclose(fout);
}