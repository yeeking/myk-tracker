#include <functional>
#include <vector> 
#include <map>
#include <string>

#pragma once

typedef std::vector<double>* VecDle;
typedef std::vector<VecDle>* VecVecDle;
typedef std::function<void(VecDle)> StepFunction;
// typedef std::function<void(VecVecDle)> StepFunction;

/** wrapper for the commands that steps in a sequence can call*/
class SequencerCommands{
    public:
        SequencerCommands();
    private:
    /** map from command names to lambdas*/
      std::map<std::string, StepFunction> functionMap;
};
