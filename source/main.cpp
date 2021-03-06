#include <iostream>
#include <string>

#include <nn/bp/net.hpp>

#ifdef NN_OPENCL
#include <nn/hw/bp/factory.hpp>
#include <nn/hw/bp/layer.hpp>
#include <nn/hw/bp/conn.hpp>
#else
#include <nn/sw/bp/layerext.hpp>
#include <nn/sw/bp/conn.hpp>
#endif

#include "reader.hpp"
#include "print.hpp"

#define PRINT_PLOT
#define TEST
#define CROSSENTROPY

#ifdef NN_OPENCL
void logProgramTime(cl::program *program)
{
	cl::map<cl::kernel *> &kernels = program->get_kernel_map();
	unsigned long total = 0;
	for(cl::kernel *k : kernels)
	{
		printf("%03lf ms, %d times : '%s'\n", k->get_time()*1e-6, k->get_count(), k->get_name());
		total += k->get_time();
		k->clear_counter();
	}
	printf("total: %03lf ms\n", total*1e-6);
}
#endif

int main(int argc, char *argv[])
{
	static const int LAYER_COUNT = 3;
	const int LAYER_SIZE[LAYER_COUNT] = {28*28, 30, 10};
	
	srand(987654);
	
#ifdef NN_OPENCL
	FactoryHW_BP factory;
#endif
	
	Net_BP net;
	
	Layer_BP *in;
	Layer_BP *out;
	
#ifdef NN_OPENCL
	for(int i = 0; i < LAYER_COUNT; ++i)
	{
		Layer_BP *layer;
		if(i != 0)
#ifdef CROSSENTROPY
			layer = factory.newLayer(i, LAYER_SIZE[i], LayerFunc::SIGMOID | LayerCost::CROSS_ENTROPY);
#else
			layer = factory.newLayer(i, LAYER_SIZE[i], LayerFunc::SIGMOID);
#endif
		else
			layer = factory.newLayer(i, LAYER_SIZE[i]);
		
		if(i == 0)
			in = layer;
		else if(i == LAYER_COUNT - 1)
			out = layer;
		net.addLayer(layer);
	}
	
	for(int i = 0; i < LAYER_COUNT - 1; ++i)
	{
		Conn_BP *conn = factory.newConn(i, LAYER_SIZE[i], LAYER_SIZE[i + 1]);
		conn->getWeight().randomize();
		conn->getBias().randomize();
		net.addConn(conn, i, i + 1);
	}
#else
	for(int i = 0; i < LAYER_COUNT; ++i)
	{
		Layer_BP *layer;
		if(i != 0)
#ifdef CROSSENTROPY
			layer = new LayerExtSW_BP<LayerFunc::SIGMOID | LayerCost::CROSS_ENTROPY>(i, LAYER_SIZE[i]);
#else
			layer = new LayerExtSW_BP<LayerFunc::SIGMOID>(i, LAYER_SIZE[i]);
#endif
		else
			layer = new LayerSW_BP(i, LAYER_SIZE[i]);
		
		if(i == 0)
			in = layer;
		else if(i == LAYER_COUNT - 1)
			out = layer;
		net.addLayer(layer);
	}
	
	for(int i = 0; i < LAYER_COUNT - 1; ++i)
	{
		Conn_BP *conn = new ConnSW_BP(i, LAYER_SIZE[i], LAYER_SIZE[i + 1]);
		conn->getWeight().randomize();
		conn->getBias().randomize();
		net.addConn(conn, i, i + 1);
	}
#endif
	
	ImageSet train_set("mnist/train-labels.idx1-ubyte", "mnist/train-images.idx3-ubyte");
	ImageSet test_set("mnist/t10k-labels.idx1-ubyte", "mnist/t10k-images.idx3-ubyte");
	
	if(train_set.getImageSizeX() != 28 || train_set.getImageSizeY() != 28)
	{
		std::cerr << "train set image size is not 28x28" << std::endl;
		return 1;
	}
	if(test_set.getImageSizeX() != 28 || test_set.getImageSizeY() != 28)
	{
		std::cerr << "test set image size is not 28x28" << std::endl;
		return 1;
	}
	
	const int batch_size = 10;
	
	float cost;
	int score;
	
	for(int k = 0; k < 0x20; ++k)
	{
#ifdef PRINT_INFO
		std::cout << "epoch " << k << ':' << std::endl;
#endif
		
		train_set.shuffle();
		
		score = 0;
		cost = 0.0f;
		for(int j = 0; j < train_set.getSize(); ++j)
		{
			const int out_size = LAYER_SIZE[LAYER_COUNT - 1];
			
			const float *in_data = train_set.getImages()[j]->getData().data();
			float out_data[out_size];
			float result[out_size];
			
			int digit = train_set.getImages()[j]->getDigit();
			for(int i = 0; i < out_size; ++i)
			{
				result[i] = i == digit ? 1.0f : 0.0f;
			}
			
			in->getInput().write(in_data);
			
			for(int i = 0; i < LAYER_COUNT; ++i)
			{
				net.stepForward();
			}
			
			out->getOutput().read(out_data);
			
			float max_val = out_data[0];
			int max_digit = 0;
			for(int i = 1; i < 10; ++i)
			{
				if(out_data[i] > max_val)
				{
					max_val = out_data[i];
					max_digit = i;
				}
			}
			if(max_digit == digit)
				++score;
			
			cost += out->getCost(result);
			out->setDesiredOutput(result);
			
			for(int i = 0; i < LAYER_COUNT - 1; ++i)
			{
				net.stepBackward();
			}
			
			if((j + 1) % batch_size == 0)
			{
				net.commitGrad(1.0f);
			}
		}
		
#if defined(PRINT_INFO)
#ifdef NN_OPENCL
		logProgramTime(factory.getProgram());
#endif // NN_OPENCL
		std::cout << "train set:" << std::endl;
		std::cout << "score: " << score << " / " << train_set.getSize() << std::endl;
		std::cout << "average cost: " << cost/train_set.getSize() << std::endl;
#elif defined(PRINT_PLOT)
		std::cout << float(score)/train_set.getSize() << " "; 
#endif
		
#ifdef TEST
		score = 0;
		for(int j = 0; j < test_set.getSize(); ++j)
		{
			const int out_size = LAYER_SIZE[LAYER_COUNT - 1];
			
			const float *in_data = test_set.getImages()[j]->getData().data();
			float out_data[out_size];
			int digit = test_set.getImages()[j]->getDigit();
			
			in->getInput().write(in_data);
			
			for(int i = 0; i < LAYER_COUNT + 1; ++i)
			{
				net.stepForward();
			}
			
			out->getOutput().read(out_data);
			
			float max_val = out_data[0];
			int max_digit = 0;
			for(int i = 1; i < 10; ++i)
			{
				if(out_data[i] > max_val)
				{
					max_val = out_data[i];
					max_digit = i;
				}
			}
			if(max_digit == digit)
				++score;
		}
		
#if defined(PRINT_INFO)
		std::cout << "test set:" << std::endl;
		std::cout << "score: " << score << " / " << test_set.getSize() << std::endl;
#elif defined(PRINT_PLOT)
		std::cout << float(score)/test_set.getSize() << " ";
		std::cout << std::endl;
#endif
#endif // TEST
	}
	
	net.forConns([](Conn *conn)
	{
		delete conn;
	});
	net.forLayers([](Layer *layer)
	{
		delete layer;
	});
	
	return 0;
}

