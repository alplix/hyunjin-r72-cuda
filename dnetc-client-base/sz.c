#include <stdio.h>
#include <stddef.h>
#include "cputypes.h"
#include "problem.h"
int main(){
  printf("sizeof(ContestWork)=%zu\n", sizeof(ContestWork));
  printf("sizeof(WorkRecord)=%zu\n", sizeof(WorkRecord));
  printf("ofs work=%zu resultcode=%zu id=%zu contest=%zu cpu=%zu os=%zu build=%zu core=%zu\n",
    offsetof(WorkRecord,work), offsetof(WorkRecord,resultcode), offsetof(WorkRecord,id),
    offsetof(WorkRecord,contest), offsetof(WorkRecord,cpu), offsetof(WorkRecord,os),
    offsetof(WorkRecord,build), offsetof(WorkRecord,core));
  return 0;
}
