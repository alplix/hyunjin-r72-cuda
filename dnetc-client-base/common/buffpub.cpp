/*
 * Copyright distributed.net 1997-2008 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 *
 * Created by Cyrus Patel <cyp@fb14.uni-mainz.de>
 *
 * Public source buffer handling stuff
*/

const char *buffpub_cpp(void) {
return "@(#)$Id: buffpub.cpp,v 1.14 2008/12/30 20:58:40 andreasb Exp $"; }

#include "cputypes.h"
#include "client.h"   //client class
#include "baseincs.h" //basic #includes
#include "util.h"     //trace
#include "logstuff.h" //Log()/LogScreen()
#include "pathwork.h" //GetFullPathForFilename()
#include "problem.h"  //Resultcode enum
#include "buffupd.h"  // BUFFERUPDATE_FETCH / BUFFERUPDATE_FLUSH
#include "buffbase.h" //the functions we're intending to export.
#include "moo521.h"   // Moo! Wrapper RC5-72 buffer file codec

/* --------------------------------------------------------------------- */

int BufferDeinitialize(Client *client)
{
  /* in theory this is where we should purge mem buffers and
  ** shutdown checkpointing but because clients need to be able to 
  ** abort by calling probfill.cpp's LoadSaveProblems(NULL,0,0) 
  ** [ie, save with abortive action], putting it here is not much use.
  */
  client = client;
  return 0;
}

int BufferInitialize(Client *client)
{
  /* in theory this is where we should restore from checkpoint
  ** but so that users can use the checkpoint as a swap buffer,
  ** we do it from ClientRun() instead.
  */
  client = client;
  return 0;
}

/* --------------------------------------------------------------------- */

int BufferZapFileRecords( const char *filename )
{
  FILE *file;
  /* truncate, don't erase buffers (especially checkpoints). This reduces */
  /* disk fragmentation (on some systems) and is better on systems with a */
  /* "trash can" that saves deleted files. */

  filename = GetFullPathForFilename( filename );
  if (access( filename, 0 )!=0) //file doesn't exist, which is ok
    return 0;
  file = fopen( filename, "w" ); //truncate the file to zero length
  if (!file)
  {
    remove(filename); //write failed, so delete it
    return -1;
  }
  fclose(file);
  return 0;
}


/* --------------------------------------------------------------------- */

static FILE *BufferOpenFile( const char *filename, 
                             unsigned long *countP, int flags )
{
  /* OSs that require "b" for fopen() */
  #if ((CLIENT_OS == OS_BEOS) || (CLIENT_OS == OS_NEXTSTEP) || \
       (CLIENT_OS == OS_RISCOS) || \
       (CLIENT_OS == OS_DOS) || (CLIENT_OS == OS_WIN32) || \
       (CLIENT_OS == OS_NETWARE) || (CLIENT_OS == OS_OS2) || \
       (CLIENT_OS == OS_WIN16) || (CLIENT_OS == OS_WIN64))
  #define BUFFOPEN_MODE "b"
  #else
  #define BUFFOPEN_MODE ""
  #endif

  FILE *file = NULL;
  long filelen;
  int failed = 0;
  const char *qfname;

  if ((flags & BUFFER_FLAGS_REMOTEBUF)!=0)
  {
    /* the remote file might be from a full client */
    LogScreen("Remote buffer fetch/flush is not supported\n");
    return NULL;
  }
  qfname = GetFullPathForFilename( filename );
  if (access(qfname, 0)!=0) // file doesn't exist, so create it
  {
    file = fopen( qfname, "w+" BUFFOPEN_MODE );
    if (file == NULL)
      failed = 1;
    else // file created. may be an exclusive open, so close and reopen
      fclose( file );
  }
  if (failed == 0)
  {
    if (access(qfname, 0)!=0) // file still doesn't exist
    {
      Log("Error opening buffer file '%s'\n"
          "Access was denied.\n", filename );
      return NULL;
    }
    file = fopen( qfname, "r+" BUFFOPEN_MODE );
    if (file == NULL)
      failed = 1;
  }
  if (failed != 0)
  {
    Log("Open failed for '%s'\n"
        "Check your file privileges (or your disk).\n", filename);
    return NULL;
  }

  if (fflush( file ) != 0 || fseek( file, 0, SEEK_END )!=0)
  {
    Log("Open failed for '%s'\n"
        "Unable to obtain directory information.\n", filename);
    fclose( file );
    return NULL;
  }
  if ((filelen = ftell(file)) == -1L)
  {
    Log("Open failed for '%s'\n"
        "Unable to determine file length.\n", filename);
    fclose( file );
    return NULL;
  }
  if ((filelen % sizeof(WorkRecord)) != 0)
  {
    Log("Open failed for '%s'\n"
        "Buffer file record count is inconsistent.\n", filename);
    fclose( file );
    return NULL;
  }
  if (countP)
  {
    *countP = (filelen / sizeof(WorkRecord));
  }    
  return file;
}

/* --------------------------------------------------------------------- */

static int BufferCloseFile( FILE *file )
{
  fclose(file);
  return 0;
}

/* --------------------------------------------------------------------- */
/* Moo! Wrapper RC5-72 buffer-file support (moo521 format)                */
/* --------------------------------------------------------------------- */

/* forward decl: defined later in this file */
static void __switch_byte_order( WorkRecord *dest, const WorkRecord *source,
                                 int from_disk );

/* Quick Moo magic detection.  Returns 1 if `filename` exists and begins
 * with the 32-byte Moo header (magic 83 B6 34 1A), 0 otherwise. */
static int moo_file_detect( const char *filename )
{
  const char *qfname = GetFullPathForFilename( filename );
  FILE *f = fopen( qfname, "rb" );
  if (!f) return 0;
  u8 magic[4];
  int is_moo = 0;
  if (fread(magic, 1, 4, f) == 4)
    is_moo = moo521_is_header( magic );
  fclose(f);
  return is_moo;
}

/* Count the non-blank Moo packets in an already-open file positioned after
 * the header.  `reccount` is the packet count from the header.
 * Mirrors the stock BufferCountFileRecords semantics: each non-blank packet
 * is decrypted, normalized to host order and, if its contest matches
 * `contest`, contributes to *packetcountP and its SWU count to *normcountP. */
static void moo_count_records( FILE *f, u32 reccount, unsigned int contest,
                               unsigned long *packetcountP,
                               unsigned long *normcountP )
{
  unsigned long packetcount = 0, normcount = 0;
  u8 pkt[MOO521_RECLEN];
  u8 blank[MOO521_RECLEN];
  memset( blank, 0, MOO521_RECLEN );
  for (u32 i = 0; i < reccount; i++)
  {
    if (fread(pkt, 1, MOO521_RECLEN, f) != MOO521_RECLEN)
      break;
    if (memcmp(pkt, blank, MOO521_RECLEN) == 0)
      continue;
    u8 body[168];
    WorkRecord wr;
    memset( &wr, 0, sizeof(wr) );
    moo521_decode( pkt, body );
    moo521_unpack( body, &wr );
    wr.contest = RC5_72;
    __switch_byte_order( &wr, &wr, 1 /* net->host */ );
    if (wr.contest == contest)
    {
      packetcount++;
      if (normcountP)
      {
        unsigned int swucount;
        if (BufferGetRecordInfo( &wr, 0, &swucount ) >= 0)
          normcount += swucount;
      }
    }
  }
  if (packetcountP) *packetcountP = packetcount;
  if (normcountP)   *normcountP   = normcount;
}

/* Read one Moo packet from an open file at the given record index.
 * Decrypts it and maps it into `data`.  Returns 0 on success,
 * +1 if the packet was blank (consumed), -1 on I/O error. */
static int moo_read_one_record( FILE *f, unsigned long reccount,
                                unsigned long recno, WorkRecord *data )
{
  u8 pkt[MOO521_RECLEN];
  u8 body[168];
  long fpos = (long)(MOO521_HEADERLEN + recno * MOO521_RECLEN);
  if (fseek(f, fpos, SEEK_SET) != 0)
    return -1;
  if ((unsigned long)fread(pkt, 1, MOO521_RECLEN, f) != MOO521_RECLEN)
    return -1;

  /* blank packet = all zeros */
  static const u8 zero_pkt[MOO521_RECLEN] = {0};
  if (memcmp(pkt, zero_pkt, MOO521_RECLEN) == 0)
    return +1; /* blank, skip */

  /* decrypt */
  moo521_decode( pkt, body );

  /* map body -> WorkRecord */
  moo521_unpack( body, data );
  /* The Moo body is a serialized WorkRecord in network (big-endian) byte
   * order (verified: contest field reads 00 00 00 05 == RC5_72).  Mirror
   * the stock BufferGetFileRecord path and normalize the `work` union
   * dwords to host order.  contest must be host-order RC5_72 first so
   * __switch_byte_order dispatches to the RC5_72 ntohl branch. */
  data->contest = RC5_72;
  __switch_byte_order( data, data, 1 /* net->host */ );
  /* On-disk resultcode is not reliably stored (the reference packet has
   * an invalid nonzero value); incoming Moo packets are work-in-progress
   * so force RESULT_WORKING like the wrapper does. */
  data->resultcode = RESULT_WORKING;

  /* blank the consumed packet */
  memset( pkt, 0, MOO521_RECLEN );
  if (fseek(f, fpos, SEEK_SET) != 0)
    return -1;
  fwrite( pkt, 1, MOO521_RECLEN, f );
  fflush( f );

  return 0;
}

/* Find the next blank Moo slot (or append) and write one packet.
 * Returns 0 on success, -1 on I/O error.  Advances *reccountP if a
 * new slot was created. */
static int moo_write_one_record( FILE *f, u32 *reccountP,
                                 const WorkRecord *data )
{
  u8 body[168];
  u8 pkt[MOO521_RECLEN];
  u8 blank[MOO521_RECLEN];
  memset( blank, 0, MOO521_RECLEN );

  /* Normalize to the Moo on-disk (network) byte order the same way the
   * stock BufferPutFileRecord does: place in `scratch` so the caller's
   * `data` is untouched, then byte-swap the `work` union (contest is
   * already RC5_72 in host order, so __switch_byte_order dispatches to
   * the RC5_72 ntohl branch). */
  WorkRecord scratch;
  __switch_byte_order( &scratch, data, 0 /* host->net */ );

  /* pack WorkRecord -> body */
  moo521_pack( &scratch, body );

  /* encode body -> encrypted packet with a fresh seed */
  u32 seed = (u32)~0u; /* fixed non-zero seed; could be randomized */
  moo521_encode( body, seed, pkt );

  /* scan for a blank slot, or append */
  unsigned long reccount = *reccountP;
  unsigned long writerec = reccount; /* default: append */
  for (unsigned long i = 0; i < reccount; i++)
  {
    u8 rd[MOO521_RECLEN];
    if (fseek(f, (long)(MOO521_HEADERLEN + i * MOO521_RECLEN), SEEK_SET) != 0)
      return -1;
    if (fread(rd, 1, MOO521_RECLEN, f) != MOO521_RECLEN)
      break;
    if (memcmp(rd, blank, MOO521_RECLEN) == 0)
    {
      writerec = i;
      break;
    }
  }

  if (writerec == reccount)
  {
    /* appending: increment header count */
    reccount++;
    /* write updated header */
    u8 hdr[MOO521_HEADERLEN];
    moo521_make_header( hdr, reccount );
    if (fseek(f, 0, SEEK_SET) != 0)
      return -1;
    fwrite( hdr, 1, MOO521_HEADERLEN, f );
    *reccountP = reccount;
  }

  /* write the packet */
  long fpos = (long)(MOO521_HEADERLEN + writerec * MOO521_RECLEN);
  if (fseek(f, fpos, SEEK_SET) != 0)
    return -1;
  if (fwrite(pkt, 1, MOO521_RECLEN, f) != MOO521_RECLEN)
    return -1;
  fflush( f );
  return 0;
}

/* --------------------------------------------------------------------- */

int UnlockBuffer( const char *filename )
{
  FILE *file = BufferOpenFile( filename, NULL, BUFFER_FLAGS_OVERRIDELOCKS );
  if (file)
  {
    BufferCloseFile( file );
    LogScreen("%s has been unlocked.\n",filename);
    return 0;
  }
  return -1; /* error message will already have been printed */
}

/* --------------------------------------------------------------------- */

int BufferNetUpdate(Client *,int, int, int, char *)
{
  return 0; /* nothing done */
}		    

/* --------------------------------------------------------------------- */

static void __switch_byte_order( WorkRecord *dest, const WorkRecord *source,
                             int from_disk /* going net->host */ )
{
  if (((const WorkRecord *)dest) != source )
    memcpy( (void *)dest, (const void *)source, sizeof(WorkRecord));

  dest->id[sizeof(dest->id)-1] = '\0';
  if (from_disk)
  {
    #if (CLIENT_OS == OS_OS390)
    __atoe(dest->id);
    #endif
  }
  else
  {
    #if (CLIENT_OS == OS_OS390)
    __etoa(dest->id);
    #endif
  }

  // Note that we do not distinguish between translating to-or-from
  // network-order, since the conversion is mathematically reversible
  // with the same operation on both little-endian and big-endian
  // machines (only on PDP machines does this assumption fail).
  switch (dest->contest)
  {
// TODO?: acidblood/trashover
    case RC5_72:
    {
      u32 *w = (u32 *)(&(dest->work));
      for (unsigned i=0; i<(sizeof(dest->work)/sizeof(u32)); i++)
        w[i] = (u32)ntohl(w[i]);
      break;
    }
    #if defined(HAVE_OGR_PASS2)
    case OGR_P2:
    {
      dest->work.ogr_p2.workstub.stub.marks  = (u16)ntohs(dest->work.ogr_p2.workstub.stub.marks);
      dest->work.ogr_p2.workstub.stub.length = (u16)ntohs(dest->work.ogr_p2.workstub.stub.length);
      dest->work.ogr_p2.minpos               = (u32)ntohl(dest->work.ogr_p2.minpos);
      for (int i = 0; i < STUB_MAX; i++)
        dest->work.ogr_p2.workstub.stub.diffs[i] = (u16)ntohs(dest->work.ogr_p2.workstub.stub.diffs[i]);
      dest->work.ogr_p2.workstub.worklength  = (u32)ntohl(dest->work.ogr_p2.workstub.worklength);
      dest->work.ogr_p2.nodes.hi             = (u32)ntohl(dest->work.ogr_p2.nodes.hi);
      dest->work.ogr_p2.nodes.lo             = (u32)ntohl(dest->work.ogr_p2.nodes.lo);
      break;
    }
    #endif
    #if defined(HAVE_OGR_CORES)
    case OGR_NG:
    {
      dest->work.ogr_ng.workstub.stub.marks  = (u16)ntohs(dest->work.ogr_ng.workstub.stub.marks);
      dest->work.ogr_ng.workstub.stub.length = (u16)ntohs(dest->work.ogr_ng.workstub.stub.length);
      dest->work.ogr_ng.workstub.collapsed   = (u16)ntohs(dest->work.ogr_ng.workstub.collapsed);
      for (int i = 0; i < OGR_STUB_MAX; i++)
        dest->work.ogr_ng.workstub.stub.diffs[i] = (u16)ntohs(dest->work.ogr_ng.workstub.stub.diffs[i]);
      dest->work.ogr_ng.workstub.worklength  = (u16)ntohs(dest->work.ogr_ng.workstub.worklength);
      dest->work.ogr_ng.nodes.hi             = (u32)ntohl(dest->work.ogr_ng.nodes.hi);
      dest->work.ogr_ng.nodes.lo             = (u32)ntohl(dest->work.ogr_ng.nodes.lo);
      break;
    }
    #endif
    default:
    {
      // PROJECT_NOT_HANDLED(dest->contest);
      dest->contest = 0xff;
      break;
    }  
  }
  return;
}

/* --------------------------------------------------------------------- */

/* on failure returns -1, else 0 */
int BufferPutFileRecord( const char *filename, const WorkRecord * data,
                         unsigned long *countP, int flags )
{
  if (moo_file_detect( filename ))
  {
    const char *qfname = GetFullPathForFilename( filename );
    FILE *f = fopen( qfname, "r+b" );
    if (!f)
    {
      f = fopen( qfname, "w+b" );
      if (!f) return -1;
      u8 hdr[MOO521_HEADERLEN];
      moo521_make_header( hdr, 0 );
      fwrite( hdr, 1, MOO521_HEADERLEN, f );
      fflush( f );
    }
    u8 hdr[MOO521_HEADERLEN];
    u32 reccount = 0;
    if (fseek(f, 0, SEEK_SET) == 0 &&
        fread(hdr, 1, MOO521_HEADERLEN, f) == MOO521_HEADERLEN)
      moo521_header_count( hdr, &reccount );
    int rc = moo_write_one_record( f, &reccount, data );
    fclose( f );
    if (countP) *countP = (rc == 0) ? reccount : 0;
    return (rc == 0) ? 0 : -1;
  }

  unsigned long reccount;
  FILE *file = BufferOpenFile( filename, &reccount, flags );
  long count = -1L;
  if ( file )
  {
    unsigned long recno = 0, writerec = reccount;
    WorkRecord blank, scratch;
    memset( (void *)&blank, 0, sizeof(blank) );
    count = 0;

    if ( fseek( file, 0, SEEK_SET ) != 0)
      count = -1L;
    while (count >= 0 && recno < reccount)
    {
      if ( fread( (void *)&scratch, sizeof(WorkRecord), 1, file ) != 1)
        count = -1L;
      else if ( 0 != memcmp( (void *)&scratch, (void *)&blank, sizeof(WorkRecord)))
        count++;
      else /* blank record. write here */	
      {
        writerec = recno; /* blank record, write here */
        if (!countP) /* don't need to count to the end */
          break;
      }	  
      recno++;
    }
    if (count >= 0)
    {
      __switch_byte_order( &scratch, data, 0 /* going host->net order */ );
      if (fseek( file, (writerec * sizeof(WorkRecord)), SEEK_SET ) != 0)
        count = -1L;
      else if (fwrite( (void *)&scratch, sizeof(WorkRecord), 1, file ) != 1)
        count = -1L;
      else	
        count++;
    }
    BufferCloseFile( file );
  }
  if (count != -1L)
  {
    if (countP) 
      *countP = count;
    return 0;
  }
  return -1;
}

/* --------------------------------------------------------------------- */

int BufferGetFileRecord( const char *filename, WorkRecord * data,
                         unsigned long *countP, int flags, int /* required_core */ ) 
                        /* returns <0 on ioerr, >0 if norecs */
{
  if (moo_file_detect( filename ))
  {
    const char *qfname = GetFullPathForFilename( filename );
    FILE *f = fopen( qfname, "r+b" );
    int rc = +1;
    if (f)
    {
      u8 hdr[MOO521_HEADERLEN];
      u32 reccount = 0;
      if (fseek(f, 0, SEEK_SET) != 0 ||
          fread(hdr, 1, MOO521_HEADERLEN, f) != MOO521_HEADERLEN)
      { fclose(f); return -1; }
      moo521_header_count( hdr, &reccount );
      for (unsigned long i = 0; i < reccount; i++)
      {
        int r = moo_read_one_record( f, reccount, i, data );
        if (r == 0)
        { rc = 0; fclose(f); break; }       /* got a record */
        if (r == -1)
        { rc = -1; fclose(f); break; }      /* I/O error */
      }
      if (rc == +1)
      {
        fclose(f);
        BufferZapFileRecords( filename );    /* all blank -> truncate */
      }
    }
    if (countP)
    {
      *countP = 0;
      if (rc == 0)
      {
        /* re-open read-only to count remaining records */
        FILE *fc = fopen( qfname, "rb" );
        if (fc)
        {
          u8 hdr2[MOO521_HEADERLEN];
          u32 rc2 = 0;
          if (fread(hdr2, 1, MOO521_HEADERLEN, fc) == MOO521_HEADERLEN)
            moo521_header_count( hdr2, &rc2 );
          moo_count_records( fc, rc2, RC5_72, countP, NULL );
          fclose( fc );
        }
      }
    }
    return rc;
  }

  unsigned long reccount = 0;
  FILE *file = BufferOpenFile( filename, &reccount, flags );
  int rc = -1;
  if ( file )
  {
    unsigned long recno = 0, blanked = 0;
    WorkRecord blank, scratch;
    memset( (void *)&blank, 0, sizeof(blank) );
    rc = +1; /* assume no records */
    if ( fseek( file, 0, SEEK_SET ) != 0)
      rc = -1;
    while (rc > 0 && recno < reccount)
    {
      if ( fread( (void *)&scratch, sizeof(WorkRecord), 1, file ) != 1)
        rc = -1;
      else if ( 0 == memcmp( (void *)&scratch, (void *)&blank, sizeof(WorkRecord)) )
        blanked++; /* blank record, ignore it */
      else if ( fseek( file, (recno * sizeof(WorkRecord)), SEEK_SET ) != 0)
        rc = -1;
      else if ( fwrite( (void *)&blank, sizeof(WorkRecord), 1, file ) != 1)
        rc = -1;
      else
      {
        blanked++;
        __switch_byte_order( data, &scratch, 1 /* going net->host order */ );
        rc = 0;
        break; /* got it */
      }	
      recno++;
    }
    BufferCloseFile( file );
    if (reccount > 0 && reccount == blanked) /* all blank */
      BufferZapFileRecords( filename );
  }
  if (countP)  
  {
    *countP = 0;
    if (rc == 0)
      BufferCountFileRecords(filename, data->contest, countP, NULL );
  }      
  return rc;
}

/* --------------------------------------------------------------------- */

int BufferCountFileRecords( const char *filename, unsigned int contest,
                       unsigned long *packetcountP, unsigned long *normcountP )
{
  if (moo_file_detect( filename ))
  {
    const char *qfname = GetFullPathForFilename( filename );
    FILE *f = fopen( qfname, "rb" );
    int failed = -1;
    unsigned long packetcount = 0, normcount = 0;
    if (f)
    {
      u8 hdr[MOO521_HEADERLEN];
      u32 reccount = 0;
      if (fread(hdr, 1, MOO521_HEADERLEN, f) == MOO521_HEADERLEN &&
          moo521_header_count(hdr, &reccount) == 0)
      {
        moo_count_records( f, reccount, contest, &packetcount, &normcount );
        failed = 0;
      }
      fclose(f);
    }
    if (packetcountP) *packetcountP = (failed == 0) ? packetcount : 0;
    if (normcountP)   *normcountP   = (failed == 0) ? normcount   : 0;
    return failed;
  }

  unsigned long normcount = 0, reccount = 0;
  FILE *file = BufferOpenFile( filename, &reccount, BUFFER_FLAGS_NOLWRITE );
  int failed = -1;
  if ( file )
  {
    unsigned long packetcount = 0, recno = 0;
    WorkRecord blank, scratch;
    memset( (void *)&blank, 0, sizeof(blank) );
    failed = 0;
    if ( fseek( file, 0, SEEK_SET ) != 0)
      failed = -1;
    while (!failed && recno < reccount)
    {
      if ( fread( (void *)&scratch, sizeof(WorkRecord), 1, file ) != 1)
        failed = -1;
      else if ( 0 == memcmp( (void *)&scratch, (void *)&blank, sizeof(WorkRecord)) )
        ; /* blank record, ignore it */
      else 
      {
        __switch_byte_order( &scratch, &scratch, 1 /* going net->host order */ );
        if ( ((unsigned int)(scratch.contest)) == contest )
        {
          packetcount++;
          if ( normcountP )
          {
            unsigned int swucount; 
            if (BufferGetRecordInfo( &scratch, 0, &swucount) >= 0)
            {
              normcount += swucount;
            }
          }    
        } /* contest is same */
      }
      recno++;
    }
    reccount = packetcount;
    BufferCloseFile( file );
  }
  if (failed != 0)
    normcount = reccount = 0;
  if (normcountP)
    *normcountP = normcount;
  if (packetcountP)
    *packetcountP = reccount;
  return failed;
}

/* --------------------------------------------------------------------- */
