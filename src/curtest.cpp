#include "Gui.h"
#include "Sequencer.h"

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


}


// 	// GridWidget grid(3, 3);
int main(){
	SimpleClock clock;
	CommandProcessor::assignMasterClock(&clock);
	if (testStepStringView()) printf("+ testStepStringView\n");
	else printf("X testStepStringView\n");

	// if (testCommandIs2()) printf("+ testCommandIs2\n");
	// else printf("X testCommandIs2\n");

	// if (testSeqGridResize1()) printf("+ testSeqGridResize1\n");
	// else printf("X testSeqGridResize1\n");
	// if (testSeqGridResize2()) printf("+ testSeqGridResize2\n");
	// else printf("X testSeqGridResize2\n");
	// if (testIncAtStep()) printf("+ testIncAtStep\n");
	// else printf("X testIncAtStep\n");
}