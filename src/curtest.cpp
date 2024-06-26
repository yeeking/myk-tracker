#include "Gui.h"
#include "Sequencer.h"
#include "SimpleClock.h"
#include "MidiUtils.h"

// bool testSeqGridResize2(){
// 	unsigned int tracks = 5, steps = 100;
// 	// create a bigger sequence
// 	Sequencer seq{tracks, steps};
// 	// create a small grid
//     std::vector<std::vector<std::string>> grid(10, std::vector<std::string>(2));
// 	// seq.prepareGridView(grid);
// 	assert(grid.size() == tracks);
// 	assert(grid[0].size() == steps);
// 	return true; 

// }


// bool testSeqGridResize1(){
// 	unsigned int tracks = 10, steps = 10;
// 	// create a bigger sequence
// 	Sequencer seq{tracks, steps};
// 	// create a small grid
// 	std::vector<std::vector<std::string>> grid;
// 	// seq.prepareGridView(grid);
// 	assert(grid.size() == tracks);
// 	assert(grid[0].size() == steps);
// 	return true; 

// }


// bool testIncAtStep(){
// 	unsigned int tracks = 10, steps = 10;
// 	// create a bigger sequence
// 	Sequencer seq{tracks, steps};
// 	SequencerEditor editor(&seq);
// 	double col0Row0Val = seq.getStepDataAt(0, 0, 0, 0);
// 	// go to step edit
// 	editor.enterAtCursor();
// 	// add a row
// 	editor.addRow();
// 	// move down to new row
// 	editor.moveCursorDown();
// 	double col0Row1Val = seq.getStepDataAt(0, 0, 1, 0);
	
// 	std::cout << "before inc row 0 "<<seq.getStepDataAt(0, 0, 0, 0) << std::endl;
// 	std::cout << "before inc row 1 "<<seq.getStepDataAt(0, 0, 1, 0) << std::endl;
	
// 	// increment the value
// 	editor.incrementAtCursor();
// 	// verify that the correct steps data has changed
// 	std::cout << "after inc row 0 "<<seq.getStepDataAt(0, 0, 0, 0) << std::endl;
// 	std::cout << "after inc row 1 "<<seq.getStepDataAt(0, 0, 1, 0) << std::endl;
	
// 	assert( seq.getStepDataAt(0, 0, 0, 0) == 0);// row 0 is not edited
// 	assert( seq.getStepDataAt(0, 0, 1, 0) == 1);// only row 1 is edited 
// 	return true; 
// }

// bool testTrigger(){
// 	Step s;
	
// 	s.setDataAt(0, Step::cmdInd, 0); // command 0
// 	s.setDataAt(0, Step::chanInd, 1);
// 	s.setDataAt(0, Step::noteInd, 1);
// 	s.setDataAt(0, Step::velInd, 1);
// 	s.setDataAt(0, Step::lengthInd, 1);


// 	s.trigger();

// 	return true; 
// }

// bool testCommandIs2()
// {
// 	Step s;
	
// 	s.setDataAt(0, Step::cmdInd, 1); // command 1
	
	
// 	assert(s.getDataAt(0, Step::cmdInd) == 1);
// 	s.trigger();

// 	return true; 
// }

bool testStepStringView(){
	Step s;
	double note = 61; 
	s.setDataAt(0, Step::noteInd, note);
	s.setDataAt(0, Step::velInd, 65);

	std::string disp = s.toStringFlat();
	std::cout << note << " goes to " << disp << std::endl; 
	return true;
}


bool testTPS(){
	Sequencer seq(2, 4);
	seq.setStepDataAt(0, 0, 0, Step::noteInd, 1);
	seq.setStepDataAt(0, 1, 0, Step::noteInd, 2);
	seq.setStepDataAt(0, 2, 0, Step::noteInd, 3);
	seq.setStepDataAt(0, 3, 0, Step::noteInd, 4);

	for(int i=0;i<24;++i){
		seq.tick();
		if (i==8){
			seq.decrementSeqParam(0, 1);
		}
	}
	seq.incrementSeqParam(0, 1);// tps + 1
	return true; 
}

bool testRandom(){
	Sequencer seq;
	seq.setStepDataAt(0, 0, 0, Step::noteInd, 32);
	seq.setStepDataAt(0, 0, 0, Step::probInd, 1.0);
	
	// seq.setStepDataAt(0, 0, 0, Step::noteInd, 32);
	

	for (int i=0;i<10;++i){
		seq.tick();
	}
	return true; 
}

bool testIncrementSeqParam(){
	Sequencer seq;
	seq.setStepDataAt(0, 0, 0, Step::noteInd, 32);
	seq.setStepDataAt(0, 0, 0, Step::probInd, 0.5);
	seq.incrementSeqParam(0, Sequence::probConfig); // should add 0.1, the step value for this param 
	assert (seq.getStepDataAt(0, 0, 0, Step::probInd) == 0.6);

	return true; 
}
bool testIncrementStepVal(){
	Sequencer seq;
	seq.setStepDataAt(0, 0, 0, Step::noteInd, 32);
	seq.setStepDataAt(0, 0, 0, Step::probInd, 0.5);
	seq.incrementStepDataAt(0, 0, 0, Step::probInd);
	assert (seq.getStepDataAt(0, 0, 0, Step::probInd) == 0.6);
	seq.decrementStepDataAt(0, 0, 0, Step::probInd);
	assert (seq.getStepDataAt(0, 0, 0, Step::probInd) == 0.5);
	
	return true; 
}

bool testSeqConf(){
	Sequencer seq;
	assert (seq.getSeqConfigSpecs().size() == 3);
	std::vector<std::vector<std::string>> conf = seq.getSequenceConfigsAsGridOfStrings();
	for (std::string& s : conf[0]){
		std::cout << s << std::endl;
	}
	assert (conf[0].size() == 3);
	return true; 
}

bool testBPM(){
	SimpleClock clock2;
	clock2.start(100);
	clock2.stop();
	double bpmStart = clock2.getBPM();
	clock2.setBPM(bpmStart + 10);
	double bpmEnd = clock2.getBPM();
	std::cout << "bpm " << bpmStart << " end " << bpmEnd << std::endl; 

	assert(bpmEnd == bpmStart + 10);
	
	return true; 
}


int main(){
	// 	// GridWidget grid(3, 3);
SimpleClock clock;
CommandProcessor::assignMasterClock(&clock);
MidiUtils midi{&clock};
CommandProcessor::assignMidiUtils(&midi);


	if (testBPM()) printf("+ testBPM\n");
	else printf("X testBPM\n");

	// if (testSeqConf()) printf("+ testSeqConf\n");
	// else printf("X testSeqConf\n");


	// if (testIncrementSeqParam()) printf("+ testIncrementSeqParam\n");
	// else printf("X testIncrementSeqParam\n");

	// if (testRandom()) printf("+ testRandom\n");
	// else printf("X testRandom\n");

	// if (testTPS()) printf("+ testTPS\n");
	// else printf("X testTPS\n");

	// if (testCommandIs2()) printf("+ testCommandIs2\n");
	// else printf("X testCommandIs2\n");

	// if (testSeqGridResize1()) printf("+ testSeqGridResize1\n");
	// else printf("X testSeqGridResize1\n");
	// if (testSeqGridResize2()) printf("+ testSeqGridResize2\n");
	// else printf("X testSeqGridResize2\n");
	// if (testIncAtStep()) printf("+ testIncAtStep\n");
	// else printf("X testIncAtStep\n");
}