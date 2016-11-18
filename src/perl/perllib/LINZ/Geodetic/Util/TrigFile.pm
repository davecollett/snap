#===============================================================================
#
# PROGRAM:             %M%
#
# VERSION:             %I%
#
# WHAT STRING:         %W%
#
# DESCRIPTION:         Perl module to interpolate from triangluated data in
#                      a binary file generated by maketrig.pl.  Note that this
#                      routine may not always work for triangulations which 
#                      do not cover a simple convex shape.
#
# DEPENDENCIES:        
#
# MODIFICATION HISTORY
# NAME                 DATE        DESCRIPTION
# ===================  ==========  ============================================
# Chris Crook          03/02/2004  Created
#===============================================================================


use strict;
use FileHandle;
use LINZ::Geodetic::Util::Unpacker;

package LINZ::Geodetic::Util::TrigFile::TrigPoint;

sub new {
   my ($class,$id,$x,$y,$data,$nodes,$opposite) = @_;
   return bless {id=>$id,x=>$x,y=>$y,data=>$data,nodes=>$nodes,opposite=>$opposite},$class;
   }

sub id { my($self)=@_; return $self->{id}; }
sub x { my($self)=@_; return $self->{x}; }
sub y { my($self)=@_; return $self->{y}; }
sub data { my($self)=@_; return $self->{data}; }
sub nodes { my($self)=@_; return $self->{nodes}; }
sub opposite { my($self)=@_; return $self->{opposite}; }

package LINZ::Geodetic::Util::TrigFile;

use vars qw/$debug/;

$debug = 0;

# Each possible triangle file format has a specified header record.  Valid records
# are listed here.  The %formats hash converts these to a format definition
# string - one of TRIG2L (little endian version 2 triangle format), 
# TRIG2B (big endian version 2 triangle format).

my $sigtrig1l = "SNAP trig binary v2.0 \r\n\x1A";
my $sigtrig1b = "CRS trig binary v2.0  \r\n\x1A";

my $siglen = length($sigtrig1l);

my %formats = (
    $sigtrig1l => 'TRIG2L',
    $sigtrig1b => 'TRIG2B' );

sub new {
   my ($class, $filename) = @_;
   
   my $fh = new FileHandle;

   # Open the  triangle file in binary mode.
   
   $fh->open($filename,'r') || die "Cannot open triangle file $filename\n";
   binmode($fh);

   my $self = {
       filename=>$filename,
       fh=>$fh,
       titles=>["Triangle mesh data from file $filename"],
       crdsyscode=>'NONE',
       offset=>0,
       embedded=>0,
       };

   bless $self, $class;
  
   $self->Setup;

   return $self;
   }

sub newEmbedded {
   my ($class, $fh, $offset) = @_;
   
   # Open the  triangle file in binary mode.
   
   my $self = {
       filename=>'Embedded',
       fh=>$fh,
       titles=>["Triangle mesh data embedded in file "],
       crdsyscode=>'NONE',
       offset=>$offset,
       embedded=>1,
       };

   bless $self, $class;
  
   seek($fh,$offset,0);
   $self->Setup;

   return $self;
   }

sub WriteToFile
{
    my($self,$filename,@options) = @_;
    my %options = ref($options[0]) ? %{$options[0]} : @options;
    my $fmt = $options{format} || $self->{output_format} || 
        ($self->{format} eq 'ASCII' ? 'TRIG2L' : 'ASCII');
    $fmt = uc($fmt);
    my $dumptopology = $options{dumptopology};
    die "Invalid trig output format - can only handle ASCII\n"
        if $fmt ne 'ASCII';

    open( my $fh, ">", $filename ) || 
        die "Cannot open triangle output file $filename\n";

    print $fh "FORMAT ",$self->{format},"\n";
    print $fh "HEADER0 ",$self->{titles}->[0],"\n";
    print $fh "HEADER1 ",$self->{titles}->[1],"\n";
    print $fh "HEADER2 ",$self->{titles}->[2],"\n";
    print $fh "CRDSYS ",$self->{crdsyscode},"\n";
    print $fh "FORMAT ",$self->{format},"\n";
    my $ndim = $self->{ndim};
    my $npts = $self->{npts};
    print $fh "NDIM ",$ndim,"\n";
    my $xy = $self->{xy};
    my $data = $self->{data};
    my $nd = 0;
    foreach my $i (0 .. $npts-1)
    {
        printf $fh "P %d %lf %lf",$i+1,$xy->[$i*2],$xy->[$i*2+1];
        foreach my $id ( 1 .. $ndim )
        {
            printf $fh " %lf",$data->[$nd];
            $nd++;
        }
        print $fh "\n";
    }
    if( $dumptopology )
    {
        foreach my $i (0 .. $npts-1)
        {
            my $itp = $self->{topoidx}->[$i];
            my $nedge = $self->{topodata}->[$itp];
            printf $fh "#PT %d %d",$i+1,$nedge;
            foreach my $edge (1 .. $nedge*2)
            {
                $itp++;
                printf $fh " %d",$self->{topodata}->[$itp];
            }
            printf $fh "\n";
        }
    }
    foreach my $i (0 .. $npts-1)
    {
        my $pt0 = $i+1;
        my $itp = $self->{topoidx}->[$i];
        my $nedge = $self->{topodata}->[$itp];
        my $pt2 = $self->{topodata}->[$itp+$nedge];
        foreach my $edge (1 .. $nedge)
        {
            my $pt1 = $pt2;
            $itp++;
            $pt2 = $self->{topodata}->[$itp];
            printf $fh "T %d %d %d\n",$pt0,$pt1,$pt2  if $pt0 < $pt1 && $pt0 < $pt2;
        }
    }
    $fh->close;
}

# Default set up for a LINZ triangle file

sub Setup {
   my ($self) = @_;

   my $filename = $self->{filename};
   my $fh = $self->{fh};
  
   # Read the triangle file signature and ensure that it is valid
   
   my $testsig;
   read($fh,$testsig,$siglen);
   
   my $fmt = $formats{$testsig};
   
   die "$filename is not a valid triangle file - signature incorrect\n" if ! $fmt;
   
   my $filebigendian = $fmt eq 'TRIG2B';
   my $unpacker = new LINZ::Geodetic::Util::Unpacker($filebigendian,$fh);
   
   # Read the triangle descriptive data
   
   my ($s1,$s2,$s3,$s4) = $unpacker->read_string(4);
   
   # Read the extents

   my ($ymn,$ymx,$xmn,$xmx) = $unpacker->read_double(4);

   # Read array sizes

   my ($npts,$ndim) = $unpacker->read_short(2);
   my ($narray) = $unpacker->read_long;

   # Read the triangle data

   my @xy = $unpacker->read_double( 2*$npts );
   my @data = $unpacker->read_double( $ndim*$npts);
   my @topoidx = $unpacker->read_long( $npts );
   my @topodata = $unpacker->read_short( $narray );

   $self->{ titles } = [$s1,$s2,$s3];
   $self->{ format } = $fmt;
   $self->{ crdsyscode } = $s4;
   $self->{ xmin } = $xmn;
   $self->{ xmax } = $xmx;
   $self->{ ymin } = $ymn;
   $self->{ ymax } = $ymx;
   $self->{ npts } = $npts;
   $self->{ ndim } = $ndim;
   $self->{ xy } = \@xy;
   $self->{ data } = \@data;
   $self->{ topoidx } = \@topoidx;
   $self->{ topodata } = \@topodata;
   $self->{ nodes } = [];
   }


sub DESTROY {
   my( $self ) = @_;
   if( ! $self->{embedded} ) {
     my $fh = $self->{fh};
     close($fh);
     }
   }

sub FileName {
   my ($self) = @_;
   return $self->{filename};
   }

sub Title {
   my ($self) = @_;
   return grep /\S/, @{$self->{titles}};
   }

sub CrdSysCode {
   my ($self) = @_;
   return $self->{crdsyscode};
   }

sub Range {
   my ($self) = @_;
   return @{$self}{ 'xmin', 'ymin', 'xmax', 'ymax' };
   }

sub Dimension {
   my ($self) = @_;
   return $self->{ndim};
   }
  
sub GetPoint {
   my( $self,$id) = @_;
   return undef if $id < 1 || $id > $self->{npts};
   my $node = $self->{nodes}->[$id];
   return $node if defined $node;
   my $idx = $id-1;
   my $x = $self->{xy}->[$idx*2];
   my $y = $self->{xy}->[$idx*2+1];
   my $ndim = $self->{ndim};
   my @data = @{$self->{data}}[$idx*$ndim .. ($idx+1)*$ndim-1];
   $idx = $self->{topoidx}->[$idx];
   my $nnode = $self->{topodata}->[$idx];
   $idx++;
   my @nodes = @{$self->{topodata}}[$idx .. $idx+$nnode-1];
   $idx += $nnode;
   my @opposite = @{$self->{topodata}}[$idx .. $idx+$nnode-1];
   $node = new LINZ::Geodetic::Util::TrigFile::TrigPoint($id,$x,$y,\@data,\@nodes,\@opposite);
   $self->{nodes}->[$id] = $node;
   return $node;
   }
   
sub FindTriangle {
   my( $self, $xt, $yt ) = @_;
   print "Seeking triangle for point $xt $yt\n" if $debug;
   if( $xt < $self->{xmin} || $xt > $self->{xmax} ||
       $yt < $self->{ymin} || $yt > $self->{ymax} ){ return undef;}

   # Look for nearest point (by rectangular distance)
   # First binary search in x direction

   my $npt = $self->{npts};
   my $xy = $self->{xy};
   my ($i0,$i1,$x0,$x1) = (0,$npt-1,$xy->[0],$xy->[$npt*2-2]);
   while( $i1-$i0 > 1 )
   {
      my $im = int(($i1+$i0)/2);
      my $xm = $xy->[$im*2];
      if( $xm < $xt ){ $i0 = $im; $x0 = $xm; } 
      else { $i1 = $im; $x1 = $xm; }
   }

   # Then move out from point to nearest in x and y 
   # directions

   my $offset = ($xt-$x0)+abs($yt-$xy->[2*$i0+1]);
   $i0--;
   my $p0 = $i0+1;
   while( $i0 >= 0 && $i1 < $npt )
   {
      if( $i0 >= 0 )
      {
         my $dx = $xt - $xy->[$i0*2];
         if( $dx > $offset ) { $i0 = -1; }
         else {
            $dx += abs($yt - $xy->[$i0*2+1]);
            if( $dx < $offset ) { $offset=$dx; $p0=$i0+1; }
            $i0--;
            }
      }
      if( $i1 < $npt )
      {
         my $dx = $xy->[$i1*2]-$xt;
         if( $dx > $offset ) { $i1 = $npt; }
         else {
            $dx += abs($yt  - $xy->[$i1*2+1]);
            if( $dx < $offset ){ $offset=$dx; $p0=$i1+1; }
            $i1++;
            }
      }
   }

   print "Starting node $p0\n" if $debug;

   # Now traverse the grid to find a triangle the point is in.
   
   my $pt0 = $self->GetPoint($p0);
   if( $pt0->x == $xt && $pt0->y == $yt )
   {
       return [$pt0,$self->GetPoint($pt0->nodes->[0]),$self->GetPoint($pt0->nodes->[1])];
   }

   my $lastp0 = $p0;
   my $finished = 0;
   while( ! $finished )
   {
      $finished = 1;
      my ($xp,$yp) = ($pt0->x,$pt0->y);
      my ($x,$y) = ($xt-$xp,$yt-$yp);

      my ($x3,$y3,$pt3,$dp3)=(0,0,0,0);
      for my $i (-1 .. $#{$pt0->nodes})
      {
         my($x2,$y2,$pt2,$dp2) = ($x3,$y3,$pt3,$dp3);
         $pt3 = $self->GetPoint($pt0->nodes->[$i]);
         if( $pt3 ){ $x3 = $pt3->x-$xp; $y3=$pt3->y-$yp; $dp3 = $x*$y3-$y*$x3; }
         next if ! $pt2 || ! $pt3;
         next if $dp2 > 0 || $dp3 <= 0;
         $x3 -= $x2; $y3 -= $y2; $x -= $x2; $y -= $y2;
         return [$pt0,$pt2,$pt3] if $x2*$y-$y3*$x >= 0;
         my $lp0 = $p0;
         $p0 = $pt0->opposite->[$i-1];
         return [$pt0,$pt2,$pt3] if $p0 == $lastp0;
         $lastp0 = $lp0;
         $pt0 = $self->GetPoint($p0);
         return undef if $pt0;
         print "Flipping to node $p0\n" if $debug;
         $finished = 0;
         last;
      }
   }
}
   

sub Calc {
   my( $self, $x, $y) = @_;
   my ($trg) = $self->FindTriangle( $x, $y );

   die "Point is not in triangulation\n" if ! $trg;

   my ($p0,$p1,$p2) = @$trg;
   print "Point in triangle ",join(' ',map {$_->id} @$trg),"\n" if $debug;

   my @m;
   my $a = ($p0->x-$p1->x)*($p2->y-$p1->y)+($p0->y-$p1->y)*($p1->x-$p2->x);
   $m[0] = (($x-$p1->x)*($p2->y-$p1->y) + ($y-$p1->y)*($p1->x-$p2->x))/$a;
   $m[1] = (($x-$p2->x)*($p0->y-$p2->y) + ($y-$p2->y)*($p2->x-$p0->x))/$a;
   $m[2] = (($x-$p0->x)*($p1->y-$p0->y) + ($y-$p0->y)*($p0->x-$p1->x))/$a;

   my @values;
   foreach my $i (0..2)
   {
     my $p = $trg->[$i];
     foreach my $j (0..$self->{ndim}-1)
     {
        $values[$j] += $p->data->[$j] * $m[$i];
     }
   }

   
   print "Calc value = ",join(' ',@values),"\n" if $debug;

   return @values;
   }

1;

