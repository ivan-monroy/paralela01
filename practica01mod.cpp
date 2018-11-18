#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#include <thread>
#include <mutex>
#include <condition_variable>

#define FEATURE_LOGGER 0
#define DEBUGGER 0


struct Resultado_hilo{
	std::thread::id id_hilo;	// ID del thread
	double resultado;		// Resultado del thread
};


struct Resultado_hilo *resultado_hilos;


struct Argumentos_hilo {
	double *array;
	int inicio;
	int fin;
	double (*funcion)(double*, int, int);
	int indice;
	int n_hilos;
};

double resultado_final;
std::mutex mx;	// Mutex para acceder a la VG resultado_final

void funcion_hilos(struct Argumentos_hilo argumentos);
double suma(double *array, int inicio, int fin);
double funcion_xor(double *array, int inicio, int fin);

#ifdef FEATURE_LOGGER
// Mutex y CV que despierta al logger
std::mutex mx_despierta_logger;
std::condition_variable cv_despierta_logger;
bool b_logger = false;

// Registro de finalizacion de hilos
int *reg;
std::mutex mx_reg; 


/**
 * Funcion del logger
 *
 * @param cv_logger: variable condicional para comunicarse con el main
 * @param mutex_logger: mutex para comunicarse con el main
 * @param logger_info: variable del main en la que depositar los datos
 * @param n_hilos: numero de hilos 
 *
 */
void logger(std::condition_variable *cv_logger, std::mutex *mx_logger,
	    double *logger_info, bool *b_main, int n_hilos)
{
	#ifdef DEBUGGER
	printf("Se ha creado el logger\n");
	#endif
	double resultado_logger = 0;	// Variable local del logger
	int hilos_terminados = 0;	// Hilos que han terminado y despiertan al logger
	
	while(hilos_terminados < n_hilos) {
		// Mientras no hayan acabado todos los hilos
		std::unique_lock<std::mutex> lk_logger(mx_despierta_logger); // Coge el mutex
		cv_despierta_logger.wait(lk_logger, [n_hilos]{return b_logger;});// Espera
		// Se despierta
		hilos_terminados += 1;
		lk_logger.unlock();	
	}
	// Terminan todos los hilos
	
	// Imprime por orden de creacion
	printf("MUESTRA RESULTADOS POR ORDEN DE CREACION\n");
	for(int i = 0; i < n_hilos; i++) {
		printf("Hilo: %d\nResultado: %f\n", i, resultado_hilos[i].resultado);
	}

	// Imprime el registro
	printf("MUESTRA RESULTADOS DEL REGISTRO\n");
	for(int i = 0; i < n_hilos; i++) {
		printf("Hilo: %d\n", reg[i]);
	}
	
	// Calcula el resultado el logger
	for(int i = 0; i < n_hilos; i++) {
		resultado_logger += resultado_hilos[i].resultado;
	}
	
	printf("El logger obtiene el resultado: %f\n", resultado_logger);
	
	// Pasa el resultado al main
	{
		std::lock_guard<std::mutex> lk_main((*mx_logger));
		(*logger_info) = resultado_logger;	// Pasa el resultado al main
		(*b_main) = true;
	}		
	(*cv_logger).notify_one();
	
	printf("Termina el hilo logger\n");
}
#endif


/** FUNCION MAIN */
int main(int argc, char *argv[]) {
	if(argc < 3) {
		printf("Faltan argumentos!\n");
		exit(-1);
	}else if(argc > 5) {	// hay un argumento de mas
		printf("El argumento %s no es valido!\n", argv[argc-1]);
		exit(-1);
	}
#ifdef FEATURE_LOGGER
	double comparacion;
#endif
    	int imprimeElRegistro = 0;  // Variable que decide quien imprime el registro 
    
	int n_elementos = atoi(argv[1]);	// Numero de elementos del array
	char *operacion = argv[2];	// Operacion a realizar
	printf("Numero elementos: %d\n Operacion %s\n", n_elementos, operacion);
	
	// Decide la funcion a utilizar por los hilos
	double (*funcion_operacion)(double*, int, int);
	// Valida la operacion introducirda
	if(strcmp(operacion, "sum") != 0 && strcmp(operacion, "xor") != 0) {
		printf("La operacion debe ser sum o xor\n");
		exit(-1);
	}else if(strcmp(operacion, "sum") == 0) {
		funcion_operacion = suma;
	}else{
		funcion_operacion = funcion_xor;
	}

	// Array
	double *elementos = (double *) malloc (n_elementos * sizeof (double));
	if(!elementos) {
		printf("Error al crear el array\n");
		exit(-1);
	}
	for(int i = 0; i<n_elementos; i++) {
		elementos[i] = (double) i;
#ifdef DEBUGGER
	//	printf("%f\n", elementos[i]);
#endif
	}
	
	if(argc < 4){	// Single threading
		
		// Resultado
		double sol = funcion_operacion(elementos, 0, n_elementos);
		printf("El resultado es %f\n", sol);
		return 0;

	}else{	// Multi threading
		char *opcion = argv[3];
		
		if(argc == 4) { 	// Falta multithread
			printf("Error, introducir numero de threads\n");
			exit(-1);
		}
		if(strcmp(opcion, "--multi-thread") != 0) {
			printf("Error, introducir un comando valido (--multi-thread)\n");
			exit(-1);
		}	
		int n_hilos = atoi(argv[4]);	
		// Valida el numero de hilos 
		if(n_hilos < 1 || n_hilos > 10) {
			printf("Error, el numero de hilos debe ser entre 1 y 10\n");
			exit(-1);
		}
#ifdef DEBUGGER
		printf("Multithreading con %d threads\n", n_hilos);
#endif
		// Reparte el array entre los hilos
		// Divide el array en n trozos
		int divisiones = ceil((n_elementos/n_hilos));
		printf("divisiones: %d\n", divisiones);
		
		// Crea los hilos
		std::thread hilos[n_hilos];

		// Crea el array de argumentos para cada hilo
		struct Argumentos_hilo *argumentos = (struct Argumentos_hilo *) malloc(n_hilos*sizeof(struct Argumentos_hilo));
		if(!argumentos) {
			printf("Error al crear el array de argumentos\n");
			exit(-1);
		}

		// Reserva para el array de resultados de hilos
		resultado_hilos = (struct Resultado_hilo *) malloc(n_hilos*sizeof(struct Resultado_hilo));
		if(!resultado_hilos) {
			printf("Error al crear el array de hilos terminados\n");
			exit(-1);
		}
		
		// Reserva para el array de registro
		reg = (int *) malloc(n_hilos*sizeof(int));
		if(!reg) {
			printf("Error al crear el array de registro\n");
			exit(-1);
		}
		// Inicializa a -1
		for(int i = 0; i < n_hilos; i++) {
			reg[i] = -1;
		}			
		
#ifdef FEATURE_LOGGER
		// Variables necesarias
		double logger_info;
		imprimeElRegistro = 1;  // 0 si imprime el Main, 1 si imprime el Logger
		std::mutex mx_logger;
		std::condition_variable cv_logger;
		bool b_main = false;
		// Crea el hilo logger
		std::thread hilo_logger = std::thread(logger, &cv_logger, &mx_logger, &logger_info, &b_main, n_hilos);
#endif
		// Comprueba si la carga se reparte correctamente o 
		// el primer hilo tiene mas carga
		int resto;
		if((n_elementos % n_hilos) != 0) {	// El primer hilo lleva mas carga
			resto = (int) (n_elementos % n_hilos); 
#ifdef DEBUGGER
			printf("El primer hilo lleva mas carga\n");
#endif	
		}else {	// Todos los hilos se reparten la misma carga
			resto = 0;
#ifdef DEBUGGER
			printf("Todos los hilos cargan igual\n");
#endif			
		}
		// Crea el hilo 0
		argumentos[0].array = elementos;
		argumentos[0].inicio = 0;
		argumentos[0].fin = divisiones+resto;
		argumentos[0].funcion = funcion_operacion;
		argumentos[0].indice = 0;
		argumentos[0].n_hilos = n_hilos;
		hilos[0] = std::thread(funcion_hilos,argumentos[0]);
		// Crea los otros hilos workers
		for(int i = 1; i<n_hilos; i++) {
			argumentos[i].array = elementos;
			argumentos[i].inicio = divisiones*i;
			argumentos[i].fin = divisiones*i+divisiones;
			argumentos[i].funcion = funcion_operacion;
			argumentos[i].indice = i;
			argumentos[i].n_hilos = n_hilos;
			// thread(array, inicio, fin, operacion, indice)
			hilos[i] = std::thread(funcion_hilos, argumentos[i]);
		}	// FIN FOR CREA HILOS
		
		// Espera a los workers
		for(int i = 0; i<n_hilos; i++) {
			hilos[i].join();
		}
#ifdef DEBUGGER
		printf("Hilos workers finalizados\n");
#endif


#ifdef FEATURE_LOGGER
		std::unique_lock<std::mutex> lk_logger_main(mx_logger);
		cv_logger.wait(lk_logger_main, [b_main]{return b_main;});	// Espera al logger
		printf("Se despierta el main\n");
		// Aqui recibe el logger_info
		hilo_logger.join();	// Join al logger
		lk_logger_main.unlock();
#endif

		if(imprimeElRegistro == 0) {    // El main imprime el registro
		    printf("MUESTRA LOS RESULTADOS\n");
		    for(int i = 0; i < n_hilos; i++) {
			    printf("Hilos: %d\nResultado: %f\n", reg[i], resultado_hilos[reg[i]].resultado);
		    }
		}
#ifdef FEATURE_LOGGER
		// Compara el resultado con el obtenido por el logger
		comparacion = resultado_final - logger_info;
		if(comparacion != 0) {
			printf("Hay un desvio = %f\n", comparacion);
			comparacion = 1;
		}
#endif
		// Muestra resultados
		printf("El resultado es: %f\n", resultado_final);
	}

#ifdef FEATURE_LOGGER
	printf("El resultado de la comparacion con el main es %d\n", comparacion);
	exit(comparacion);
#endif
	return 0;
}

/**
 * Funcion de cada hilo
 */
void funcion_hilos(struct Argumentos_hilo argumentos) {
	
	double resultado = 0;
    	// Calcula el resultado parcial de la funcion
	resultado = argumentos.funcion(argumentos.array, argumentos.inicio, argumentos.fin);
	// Guarda los resultados
	resultado_hilos[argumentos.indice].id_hilo = std::this_thread::get_id();
	resultado_hilos[argumentos.indice].resultado = resultado;
	printf("El hilo %d guarda el resultado %f\n", argumentos.indice, resultado_hilos[argumentos.indice].resultado);
    

	// Guarda los resultados
	printf("Guarda los resultados en la variable global\n");
	{
		std::lock_guard<std::mutex> lock_fin(mx);
		resultado_final += resultado;
    	}	
    
#ifdef FEATURE_LOGGER
    
	printf("Pasa resultados al logger\n");
    	/*DESPIERTA AL lOGGER*/
	{
		std::lock_guard<std::mutex> lk_despierta_logger(mx_despierta_logger); // Coge el mutex
		b_logger = true;
	}
	cv_despierta_logger.notify_one(); // Despierta al logger
#endif
    // Registra cuando acaba
    std::lock_guard<std::mutex> lk_reg(mx_reg);
    for(int i = 0; i < argumentos.n_hilos; i++) {
    	if(reg[i] == -1) {
    		reg[i] = argumentos.indice;
		break;
    	}
    }
}

/**
 *Funcion que suma el array que se le pasa
 *por parametro
 */
double suma(double *array, int inicio, int fin) {
	double resultado = 0;
	for(int i = inicio; i<fin; i++) {
		resultado += array[i];
	}
	return resultado;
}


/**
 * Funcion que hace un xor en el array
 * que se pasa por parametro
 */
double funcion_xor(double *array, int inicio, int fin) {
	int resultado = 0;
	for(int i=inicio; i<fin; i++) {
		resultado ^= (int) array[i];
	}
	return resultado;
}
