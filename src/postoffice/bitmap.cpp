#include <stdio.h>
#include "bitmap.h"

Bitmap24::Bitmap24 (void)
{
	Bitmap = NULL;
}

Bitmap24::~Bitmap24 (void)
{
	int i;
	if (Bitmap)
	{
		for (i=0;i<Height;++i) delete Bitmap[i];
		delete (Bitmap);
	}
}

void Bitmap24::SaveJPEG (char *FN, int QTY)
{
	struct jpeg_compress_struct		 cinfo;
	struct jpeg_error_mgr			 jerr;
	JSAMPLE							*image_buffer = (JSAMPLE *) &Bitmap;
	int								 image_height = Height;
	int								 image_width = Width;
	
	FILE							*outfile;
	JSAMPROW						 row_pointer;
	int								 row_stride;
	
	cinfo.err = jpeg_std_error (&jerr);
	jpeg_create_compress (&cinfo);
	
	if ((outfile = fopen (FN, "w")) == NULL)
	{
		return;
	}
	jpeg_stdio_dest (&cinfo, outfile);
	
	cinfo.image_width = image_width;
	cinfo.image_height = image_height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	
	jpeg_set_defaults (&cinfo);
	
	jpeg_set_quality (&cinfo, QTY, TRUE);
	
	jpeg_start_compress (&cinfo, TRUE);
	
	row_stride = image_width * 3;
	
	while (cinfo.next_scanline < cinfo.image_height)
	{
		row_pointer = (JSAMPLE *) Bitmap[cinfo.next_scanline];
		jpeg_write_scanlines (&cinfo, &row_pointer, 1);
	}
	jpeg_finish_compress (&cinfo);
	fclose (outfile);
	
	jpeg_destroy_compress (&cinfo);
}

void Bitmap24::PutJPEG (FILE *outfile, int QTY)
{
	struct jpeg_compress_struct		 cinfo;
	struct jpeg_error_mgr			 jerr;
	JSAMPLE							*image_buffer = (JSAMPLE *) &Bitmap;
	int								 image_height = Height;
	int								 image_width = Width;
	
	JSAMPROW						 row_pointer;
	int								 row_stride;
	
	cinfo.err = jpeg_std_error (&jerr);
	jpeg_create_compress (&cinfo);
	
	jpeg_stdio_dest (&cinfo, outfile);
	
	cinfo.image_width = image_width;
	cinfo.image_height = image_height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	
	jpeg_set_defaults (&cinfo);
	
	jpeg_set_quality (&cinfo, QTY, TRUE);
	
	jpeg_start_compress (&cinfo, TRUE);
	
	row_stride = image_width * 3;
	
	while (cinfo.next_scanline < cinfo.image_height)
	{
		row_pointer = (JSAMPLE *) Bitmap[cinfo.next_scanline];
		jpeg_write_scanlines (&cinfo, &row_pointer, 1);
	}
	jpeg_finish_compress (&cinfo);
	
	jpeg_destroy_compress (&cinfo);
}

void Bitmap24::LoadJPEG (char *FN)
{
	struct jpeg_decompress_struct	 cinfo;
	struct jpeg_error_mgr			 jerr;
	FILE							*infile;
	int								 row_stride;
	int								 i;
	
	if ((infile = fopen (FN, "r")) == NULL)
	{
		return;
	}

	cinfo.err = jpeg_std_error (&jerr);
	
	jpeg_create_decompress (&cinfo);
	jpeg_stdio_src (&cinfo, infile);
	jpeg_read_header (&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);
	if (cinfo.output_components == 3)
	{
		Bitmap = new unsigned char *[cinfo.output_height];
		for (i=0;i<cinfo.output_height;++i) Bitmap[i] = new unsigned char[3*cinfo.output_width];
		Width = cinfo.output_width;
		Height = cinfo.output_height;
		while (cinfo.output_scanline < Height)
			jpeg_read_scanlines (&cinfo, &Bitmap[cinfo.output_scanline], 1);
	}
	jpeg_finish_decompress (&cinfo);
	jpeg_destroy_decompress (&cinfo);
}

void Bitmap24::Create (int W, int H)
{
	int i;
	
	Width = W;
	Height = H;
	
	Bitmap = new unsigned char *[Height];
	for (i=0;i<Height;++i) Bitmap[i] = new unsigned char [3*Width];
}

void Bitmap24::Rect (int W, int H, int R, int G, int B)
{
	int y,x;
	Create (W,H);
	
	for (y=0;y<Height;++y)
	{
		for (x=0;x<Width;++x)
		{
			Bitmap[y][(3*x)+0] = R;
			Bitmap[y][(3*x)+1] = G;
			Bitmap[y][(3*x)+2] = B;
		}
	}
}

#define bmap0 Bitmap[y+Y][3*(X+x)]
#define bmap1 Bitmap[y+Y][3*(X+x)+1]
#define bmap2 Bitmap[y+Y][3*(X+x)+2]

void Bitmap24::OverlayTTBitmap (char *bmap, int W, int H, int X, int Y, unsigned char R, unsigned char G, unsigned char B, int O)
{
	int x,y;
	int glyval;

	if (X+W > Width) return;
	if (Y+H > Height) return;
	
	for (y=0;y<H;++y)
	{
		for (x=0;x<W;++x)
		{
			glyval = bmap[x + (W*y)];
			if (glyval > 4) glyval = 4;
			
			bmap0 = ( ( (100-O) * (int) bmap0) + (O * (int)(( (bmap0 * (4-glyval)) + (R *glyval))/4)))/100;
			bmap1 = ( ( (100-O) * (int) bmap1) + (O * (int)(( (bmap1 * (4-glyval)) + (G *glyval))/4)))/100;
			bmap2 = ( ( (100-O) * (int) bmap2) + (O * (int)(( (bmap2 * (4-glyval)) + (B *glyval))/4)))/100;
			
		}
	}
}

void Bitmap24::OverlayBitmap24 (Bitmap24 *B, int X, int Y, int OP)
{
	int x,y;
	for (y=0;y<B->Height;++y)
	{
		for (x=0;x<B->Width;++x)
		{
			if ((y+Y < Height) && (x+X < Width))
			{
				bmap0 = (((100-OP) * (int) bmap0) + (OP * (int) B->Bitmap[y][3*x]))/100;
				bmap1 = (((100-OP) * (int) bmap1) + (OP * (int) B->Bitmap[y][3*x+1]))/100;
				bmap2 = (((100-OP) * (int) bmap2) + (OP * (int) B->Bitmap[y][3*x+2]))/100;
			}
		}
	}
}

void Bitmap24::OverlayColor (int X, int Y, unsigned char R, unsigned char G, unsigned char B, int OP)
{
	int x=0;
	int y=0;
	
	if ((X<Width) && (Y<Height))
	{
		bmap0 = (((100-OP) * (int) bmap0) + (OP * (int) R))/100;
		bmap1 = (((100-OP) * (int) bmap1) + (OP * (int) G))/100;
		bmap2 = (((100-OP) * (int) bmap2) + (OP * (int) B))/100;
	}
}

Bitmap24 *Bitmap24::Clip (int X, int Y, int W, int H)
{
	int x, y;

	//if ((X+W) >Width) W = (Width - X);
	//if ((Y+H) >Height) H = (Height - Y);

	Bitmap24 *B = new Bitmap24;
	B->Create (W, H);
	
	for (y=0;y<H;++y)
	{
		for (x=0;x<W;++x)
		{
			B->Bitmap[y][3*x]   = Bitmap[(Y+y)%Height][3*((x+X)%Width)+0];
			B->Bitmap[y][3*x+1] = Bitmap[(Y+y)%Height][3*((x+X)%Width)+1];
			B->Bitmap[y][3*x+2] = Bitmap[(Y+y)%Height][3*((x+X)%Width)+2];
		}
	}
	return B;
}








