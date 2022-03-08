#include "algorithm.h"

#include <boost/heap/binomial_heap.hpp>
#include <vector>

#include "fstream"
#include "yaml-cpp/yaml.h"

using namespace HybridAStar;

float aStar(Node2D& start, Node2D& goal, Node2D* nodes2D, int width, int height, CollisionDetection& configurationSpace,
            Visualize& visualization);
void  updateH(Node3D& start, const Node3D& goal, Node2D* nodes2D, float* dubinsLookup, int width, int height,
              CollisionDetection& configurationSpace, Visualize& visualization);
Node3D* dubinsShot(Node3D& start, const Node3D& goal, CollisionDetection& configurationSpace);

//###################################################
//                                    NODE COMPARISON
//###################################################
/*!
   \brief A structure to sort nodes in a heap structure
*/
struct CompareNodes
{
    /// Sorting 3D nodes by increasing C value - the total estimated cost
    bool operator()(const Node3D* lhs, const Node3D* rhs) const
    {
        return lhs->getC() > rhs->getC();
    }
    /// Sorting 2D nodes by increasing C value - the total estimated cost
    bool operator()(const Node2D* lhs, const Node2D* rhs) const
    {
        return lhs->getC() > rhs->getC();
    }
};

int Node3D::succ_size_         = 6;
int Node3D::forward_size_      = 3;
int Node3D::pre_succ_size_     = 6;
int Node3D::pre_forward_size_  = 3;
int Node3D::dist_succ_size_    = 6;
int Node3D::dist_forward_size_ = 3;

std::vector<float> Node3D::delta_x_               = {1, 2};
std::vector<float> Node3D::delta_y_               = {1, 2};
std::vector<float> Node3D::step_size_             = {1, 2};
std::vector<float> Node3D::delta_t_edg_           = {1, 2};
std::vector<float> Node3D::delta_t_               = {1, 2};
std::vector<float> Node3D::distance_              = {1, 2};
std::vector<float> Node3D::value_                 = {1, 2};
std::vector<float> Node3D::coef_                  = {1, 2};
bool               Node3D::change_step_           = false;
bool               Node3D::use_new_mode_          = false;
bool               Node3D::use_dist_mode_         = false;
float              Node3D::max_front_wheel_angle_ = 0;

//###################################################
//                                        3D A*
//###################################################
Node3D* Algorithm::hybridAStar(Node3D& start, const Node3D& goal, Node3D* nodes3D, Node2D* nodes2D, int width,
                               int height, CollisionDetection& configurationSpace, float* dubinsLookup,
                               Visualize& visualization, DynamicVoronoi& voronoi)
{
    // DEBUG
    ofstream debugout("/home/holo/catkin_ws/debug/debug.txt", ios::app);
    // debugout << "x"
    //          << "\t"
    //          << "X"
    //          << "\t"
    //          << "iX"
    //          << "y"
    //          << "\t"
    //          << "Y"
    //          << "\t"
    //          << "iY" << std::endl;

    // init config
    {
        YAML::Node param = YAML::LoadFile("/home/holo/catkin_ws/src/hybrid-a-star/param/param.yaml");

        Node3D::dist_succ_size_    = param["dist_succ_size"].as<int>();
        Node3D::dist_forward_size_ = param["dist_forward_size"].as<int>();
        Node3D::pre_succ_size_     = param["pre_succ_size"].as<int>();
        Node3D::pre_forward_size_  = param["pre_forward_size"].as<int>();

        Node3D::delta_x_ = param["delta_x"].as<std::vector<float>>();
        Node3D::delta_y_ = param["delta_y"].as<std::vector<float>>();
        Node3D::delta_t_ = param["delta_t_rad"].as<std::vector<float>>();

        Node3D::step_size_   = param["step_size"].as<std::vector<float>>();
        Node3D::delta_t_edg_ = param["delta_t_edg"].as<std::vector<float>>();

        Node3D::distance_              = param["distance"].as<std::vector<float>>();
        Node3D::value_                 = param["value"].as<std::vector<float>>();
        Node3D::coef_                  = param["coef"].as<std::vector<float>>();
        Node3D::change_step_           = param["change_step"].as<bool>();
        Node3D::use_new_mode_          = param["use_new_mode"].as<bool>();
        Node3D::use_dist_mode_         = param["use_dist_mode"].as<bool>();
        Node3D::max_front_wheel_angle_ = param["max_front_wheel_angle"].as<float>();

        if (Node3D::use_dist_mode_)
        {
            Node3D::succ_size_    = Node3D::dist_succ_size_;
            Node3D::forward_size_ = Node3D::dist_forward_size_;
        }
        else
        {
            Node3D::succ_size_    = Node3D::pre_succ_size_;
            Node3D::forward_size_ = Node3D::pre_forward_size_;
        }

        for (int i = 0; i < Node3D::pre_succ_size_; ++i)
        {
            Node3D::step_size_[i] *= 0.1178097;
            if (i < Node3D::pre_forward_size_)
            {
                Node3D::delta_t_[i] = Node3D::delta_t_edg_[i] * M_PI / 180;
                Node3D::delta_x_[i] = Node3D::step_size_[i] * fabs(cos(Node3D::delta_t_[i]));
                Node3D::delta_y_[i] = (-1) * Node3D::step_size_[i] * sin(Node3D::delta_t_[i]);
            }
            else
            {
                Node3D::delta_t_[i] = Node3D::delta_t_edg_[i] / 180 * M_PI;
                Node3D::delta_x_[i] = Node3D::step_size_[i] * fabs(cos(Node3D::delta_t_[i])) * (-1);
                Node3D::delta_y_[i] = Node3D::step_size_[i] * sin(Node3D::delta_t_[i]);
            }
        }
    }

    // PREDECESSOR AND SUCCESSOR INDEX
    int   iPred, iSucc;
    float newG;
    // Number of possible directions, 3 for forward driving and an additional 3 for reversing
    // int dir = Constants::reverse ? 6 : 3;
    // Number of iterations the algorithm has run for stopping based on Constants::iterations
    int iterations = 0;

    // VISUALIZATION DELAY
    ros::Duration d(0.003);

    // OPEN LIST AS BOOST IMPLEMENTATION
    typedef boost::heap::binomial_heap<Node3D*, boost::heap::compare<CompareNodes>> priorityQueue;
    priorityQueue                                                                   O;

    // update h value
    updateH(start, goal, nodes2D, dubinsLookup, width, height, configurationSpace, visualization);
    // mark start as open
    start.open();
    // push on priority queue aka open list
    O.push(&start);
    iPred          = start.setIdx(width, height);
    nodes3D[iPred] = start;

    // NODE POINTER
    Node3D* nPred;
    Node3D* nSucc;

    // float max = 0.f;

    // continue until O empty
    while (!O.empty())
    {
        //    // DEBUG
        //    Node3D* pre = nullptr;
        //    Node3D* succ = nullptr;

        //    std::cout << "\t--->>>" << std::endl;

        //    for (priorityQueue::ordered_iterator it = O.ordered_begin(); it != O.ordered_end(); ++it) {
        //      succ = (*it);
        //      std::cout << "VAL"
        //                << " | C:" << succ->getC()
        //                << " | x:" << succ->getX()
        //                << " | y:" << succ->getY()
        //                << " | t:" << helper::toDeg(succ->getT())
        //                << " | i:" << succ->getIdx()
        //                << " | O:" << succ->isOpen()
        //                << " | pred:" << succ->getPred()
        //                << std::endl;

        //      if (pre != nullptr) {

        //        if (pre->getC() > succ->getC()) {
        //          std::cout << "PRE"
        //                    << " | C:" << pre->getC()
        //                    << " | x:" << pre->getX()
        //                    << " | y:" << pre->getY()
        //                    << " | t:" << helper::toDeg(pre->getT())
        //                    << " | i:" << pre->getIdx()
        //                    << " | O:" << pre->isOpen()
        //                    << " | pred:" << pre->getPred()
        //                    << std::endl;
        //          std::cout << "SCC"
        //                    << " | C:" << succ->getC()
        //                    << " | x:" << succ->getX()
        //                    << " | y:" << succ->getY()
        //                    << " | t:" << helper::toDeg(succ->getT())
        //                    << " | i:" << succ->getIdx()
        //                    << " | O:" << succ->isOpen()
        //                    << " | pred:" << succ->getPred()
        //                    << std::endl;

        //          if (pre->getC() - succ->getC() > max) {
        //            max = pre->getC() - succ->getC();
        //          }
        //        }
        //      }

        //      pre = succ;
        //    }

        // pop node with lowest cost from priority queue
        nPred = O.top();
        // set index
        iPred = nPred->setIdx(width, height);
        iterations++;

        // RViz visualization
        if (Constants::visualization)
        {
            visualization.publishNode3DPoses(*nPred);
            visualization.publishNode3DPose(*nPred);
            d.sleep();
        }

        // debug
        // float x1 = nPred->getX();
        // float y1 = nPred->getY();
        // int   X  = (int)x1;
        // int   Y  = (int)y1;
        // int   iX = (int)((x1 - (long)x1) * Constants::positionResolution);  //得出X方向在cell中的偏移量
        // int   iY = (int)((y1 - (long)y1) * Constants::positionResolution);  // Y方向在cell中的偏移量
        // debugout << x1 << "\t" << (long)x1 << "\t" << X << "\t" << iX << "\t" << y1 << "\t" << (long)y1 << "\t" << Y
        //          << "\t" << iY << std::endl;

        // _____________________________
        // LAZY DELETION of rewired node
        // if there exists a pointer this node has already been expanded
        if (nodes3D[iPred].isClosed())
        {
            // pop node from the open list and start with a fresh node
            O.pop();
            continue;
        }
        // _________________
        // EXPANSION OF NODE
        else if (nodes3D[iPred].isOpen())
        {
            // add node to closed list
            nodes3D[iPred].close();
            // remove node from open list
            O.pop();

            // _________
            // GOAL TEST
            if (*nPred == goal || iterations > Constants::iterations)
            {
                // debug
                debugout << "use new mode: " << Node3D::use_new_mode_ << "\t"
                         << "iterations is: " << iterations << std::endl;
                // DEBUG
                return nPred;
            }

            // ____________________
            // CONTINUE WITH SEARCH
            else
            {
                // _______________________
                // SEARCH WITH DUBINS SHOT
                if (Constants::dubinsShot && nPred->getDist(goal) < 8 && nPred->isInRange(goal) &&
                    nPred->getPrim() < Node3D::forward_size_)
                {
                    nSucc = dubinsShot(*nPred, goal, configurationSpace);

                    if (nSucc != nullptr)
                    {
                        // debug
                        debugout << "use new mode: " << Node3D::use_new_mode_ << "\t"
                                 << "iterations is: " << iterations << std::endl;
                        // DEBUG
                        //  std::cout << "max diff " << max << std::endl;
                        return nSucc;
                    }
                }

                // debug
                // debugout << voronoi.getDistance(nPred->getX(), nPred->getY()) << std::endl;

                float step_size;
                if (Node3D::use_dist_mode_)
                {
                    int   index       = 0;
                    float dist_to_obl = voronoi.getDistance(nPred->getX(), nPred->getY());
                    for (; index < Node3D::distance_.size(); ++index)
                    {
                        if (dist_to_obl >= Node3D::distance_[index])
                        {
                            break;
                        }
                    }
                    step_size = Node3D::value_[index];

                    // debug
                    // debugout << dist_to_obl << "\t" << step_size << std::endl;
                }

                // ______________________________
                // SEARCH WITH FORWARD SIMULATION
                for (int i = 0; i < Node3D::succ_size_; i++)
                {
                    // create possible successor
                    if (Node3D::use_dist_mode_)
                    {
                        nSucc = nPred->dist_createSuccessor(i, step_size);
                    }
                    else if (Node3D::use_new_mode_)
                    {
                        nSucc     = nPred->new_createSuccessor(i);
                        step_size = Node3D::delta_x_[0];
                    }
                    else
                    {
                        nSucc     = nPred->createSuccessor(i);
                        step_size = Node3D::dx[0];
                    }

                    // set index of the successor
                    iSucc = nSucc->setIdx(width, height);

                    // ensure successor is on grid and traversable
                    if (nSucc->isOnGrid(width, height) && configurationSpace.isTraversable(nSucc))
                    {
                        // ensure successor is not on closed list or it has the same index as the predecessor
                        if (!nodes3D[iSucc].isClosed() || iPred == iSucc)
                        {
                            // calculate new G value
                            nSucc->updateG(Node3D::dx[0]);
                            newG = nSucc->getG();

                            // if successor not on open list or found a shorter way to the cell
                            if (!nodes3D[iSucc].isOpen() || newG < nodes3D[iSucc].getG() || iPred == iSucc)
                            {
                                // calculate H value
                                updateH(*nSucc, goal, nodes2D, dubinsLookup, width, height, configurationSpace,
                                        visualization);

                                // if the successor is in the same cell but the C value is larger
                                if (iPred == iSucc && nSucc->getC() > nPred->getC() + Constants::tieBreaker)
                                {
                                    delete nSucc;
                                    continue;
                                }
                                // if successor is in the same cell and the C value is lower, set predecessor to
                                // predecessor of predecessor
                                else if (iPred == iSucc && nSucc->getC() <= nPred->getC() + Constants::tieBreaker)
                                {
                                    nSucc->setPred(nPred->getPred());
                                }

                                if (nSucc->getPred() == nSucc)
                                {
                                    std::cout << "looping";
                                }

                                // put successor on open list
                                nSucc->open();
                                nodes3D[iSucc] = *nSucc;
                                O.push(&nodes3D[iSucc]);
                                delete nSucc;
                            }
                            else
                            {
                                delete nSucc;
                            }
                        }
                        else
                        {
                            delete nSucc;
                        }
                    }
                    else
                    {
                        delete nSucc;
                    }
                }
            }
        }
    }

    if (O.empty())
    {
        return nullptr;
    }

    // debug
    debugout.close();

    return nullptr;
}

//###################################################
//                                        2D A*
//###################################################
float aStar(Node2D& start, Node2D& goal, Node2D* nodes2D, int width, int height, CollisionDetection& configurationSpace,
            Visualize& visualization)
{
    // PREDECESSOR AND SUCCESSOR INDEX
    int   iPred, iSucc;
    float newG;

    // reset the open and closed list
    for (int i = 0; i < width * height; ++i)
    {
        nodes2D[i].reset();
    }

    // VISUALIZATION DELAY
    ros::Duration d(0.001);

    boost::heap::binomial_heap<Node2D*, boost::heap::compare<CompareNodes>> O;
    // update h value
    start.updateH(goal);
    // mark start as open
    start.open();
    // push on priority queue
    O.push(&start);
    iPred          = start.setIdx(width);
    nodes2D[iPred] = start;

    // NODE POINTER
    Node2D* nPred;
    Node2D* nSucc;

    // continue until O empty
    while (!O.empty())
    {
        // pop node with lowest cost from priority queue
        nPred = O.top();
        // set index
        iPred = nPred->setIdx(width);

        // _____________________________
        // LAZY DELETION of rewired node
        // if there exists a pointer this node has already been expanded
        if (nodes2D[iPred].isClosed())
        {
            // pop node from the open list and start with a fresh node
            O.pop();
            continue;
        }
        // _________________
        // EXPANSION OF NODE
        else if (nodes2D[iPred].isOpen())
        {
            // add node to closed list
            nodes2D[iPred].close();
            nodes2D[iPred].discover();

            // RViz visualization
            if (Constants::visualization2D)
            {
                visualization.publishNode2DPoses(*nPred);
                visualization.publishNode2DPose(*nPred);
                //        d.sleep();
            }

            // remove node from open list
            O.pop();

            // _________
            // GOAL TEST
            if (*nPred == goal)
            {
                return nPred->getG();
            }
            // ____________________
            // CONTINUE WITH SEARCH
            else
            {
                // _______________________________
                // CREATE POSSIBLE SUCCESSOR NODES
                for (int i = 0; i < Node2D::dir; i++)
                {
                    // create possible successor
                    nSucc = nPred->createSuccessor(i);
                    // set index of the successor
                    iSucc = nSucc->setIdx(width);

                    // ensure successor is on grid ROW MAJOR
                    // ensure successor is not blocked by obstacle
                    // ensure successor is not on closed list
                    if (nSucc->isOnGrid(width, height) && configurationSpace.isTraversable(nSucc) &&
                        !nodes2D[iSucc].isClosed())
                    {
                        // calculate new G value
                        nSucc->updateG();
                        newG = nSucc->getG();

                        // if successor not on open list or g value lower than before put it on open list
                        if (!nodes2D[iSucc].isOpen() || newG < nodes2D[iSucc].getG())
                        {
                            // calculate the H value
                            nSucc->updateH(goal);
                            // put successor on open list
                            nSucc->open();
                            nodes2D[iSucc] = *nSucc;
                            O.push(&nodes2D[iSucc]);
                            delete nSucc;
                        }
                        else
                        {
                            delete nSucc;
                        }
                    }
                    else
                    {
                        delete nSucc;
                    }
                }
            }
        }
    }

    // return large number to guide search away
    return 1000;
}

//###################################################
//                                         COST TO GO
//###################################################
void updateH(Node3D& start, const Node3D& goal, Node2D* nodes2D, float* dubinsLookup, int width, int height,
             CollisionDetection& configurationSpace, Visualize& visualization)
{
    float dubinsCost     = 0;
    float reedsSheppCost = 0;
    float twoDCost       = 0;
    float twoDoffset     = 0;

    // if dubins heuristic is activated calculate the shortest path
    // constrained without obstacles
    if (Constants::dubins)
    {
        // ONLY FOR dubinsLookup
        //    int uX = std::abs((int)goal.getX() - (int)start.getX());
        //    int uY = std::abs((int)goal.getY() - (int)start.getY());
        //    // if the lookup table flag is set and the vehicle is in the lookup area
        //    if (Constants::dubinsLookup && uX < Constants::dubinsWidth - 1 && uY < Constants::dubinsWidth - 1) {
        //      int X = (int)goal.getX() - (int)start.getX();
        //      int Y = (int)goal.getY() - (int)start.getY();
        //      int h0;
        //      int h1;

        //      // mirror on x axis
        //      if (X >= 0 && Y <= 0) {
        //        h0 = (int)(helper::normalizeHeadingRad(M_PI_2 - t) / Constants::deltaHeadingRad);
        //        h1 = (int)(helper::normalizeHeadingRad(M_PI_2 - goal.getT()) / Constants::deltaHeadingRad);
        //      }
        //      // mirror on y axis
        //      else if (X <= 0 && Y >= 0) {
        //        h0 = (int)(helper::normalizeHeadingRad(M_PI_2 - t) / Constants::deltaHeadingRad);
        //        h1 = (int)(helper::normalizeHeadingRad(M_PI_2 - goal.getT()) / Constants::deltaHeadingRad);

        //      }
        //      // mirror on xy axis
        //      else if (X <= 0 && Y <= 0) {
        //        h0 = (int)(helper::normalizeHeadingRad(M_PI - t) / Constants::deltaHeadingRad);
        //        h1 = (int)(helper::normalizeHeadingRad(M_PI - goal.getT()) / Constants::deltaHeadingRad);

        //      } else {
        //        h0 = (int)(t / Constants::deltaHeadingRad);
        //        h1 = (int)(goal.getT() / Constants::deltaHeadingRad);
        //      }

        //      dubinsCost = dubinsLookup[uX * Constants::dubinsWidth * Constants::headings * Constants::headings
        //                                + uY *  Constants::headings * Constants::headings
        //                                + h0 * Constants::headings
        //                                + h1];
        //    } else {

        /*if (Constants::dubinsShot && std::abs(start.getX() - goal.getX()) >= 10 && std::abs(start.getY() -
         * goal.getY()) >= 10)*/
        //      // start
        //      double q0[] = { start.getX(), start.getY(), start.getT()};
        //      // goal
        //      double q1[] = { goal.getX(), goal.getY(), goal.getT()};
        //      DubinsPath dubinsPath;
        //      dubins_init(q0, q1, Constants::r, &dubinsPath);
        //      dubinsCost = dubins_path_length(&dubinsPath);

        ompl::base::DubinsStateSpace dubinsPath(Constants::r);
        State*                       dbStart = (State*)dubinsPath.allocState();
        State*                       dbEnd   = (State*)dubinsPath.allocState();
        dbStart->setXY(start.getX(), start.getY());
        dbStart->setYaw(start.getT());
        dbEnd->setXY(goal.getX(), goal.getY());
        dbEnd->setYaw(goal.getT());
        dubinsCost = dubinsPath.distance(dbStart, dbEnd);
    }

    // if reversing is active use a
    if (Constants::reverse && !Constants::dubins)
    {
        //    ros::Time t0 = ros::Time::now();
        ompl::base::ReedsSheppStateSpace reedsSheppPath(Constants::r);
        State*                           rsStart = (State*)reedsSheppPath.allocState();
        State*                           rsEnd   = (State*)reedsSheppPath.allocState();
        rsStart->setXY(start.getX(), start.getY());
        rsStart->setYaw(start.getT());
        rsEnd->setXY(goal.getX(), goal.getY());
        rsEnd->setYaw(goal.getT());
        reedsSheppCost = reedsSheppPath.distance(rsStart, rsEnd);
        //    ros::Time t1 = ros::Time::now();
        //    ros::Duration d(t1 - t0);
        //    std::cout << "calculated Reed-Sheep Heuristic in ms: " << d * 1000 << std::endl;
    }

    // if twoD heuristic is activated determine shortest path
    // unconstrained with obstacles
    if (Constants::twoD && !nodes2D[(int)start.getY() * width + (int)start.getX()].isDiscovered())
    {
        //    ros::Time t0 = ros::Time::now();
        // create a 2d start node
        Node2D start2d(start.getX(), start.getY(), 0, 0, nullptr);
        // create a 2d goal node
        Node2D goal2d(goal.getX(), goal.getY(), 0, 0, nullptr);
        // run 2d astar and return the cost of the cheapest path for that node
        nodes2D[(int)start.getY() * width + (int)start.getX()].setG(
            aStar(goal2d, start2d, nodes2D, width, height, configurationSpace, visualization));
        //    ros::Time t1 = ros::Time::now();
        //    ros::Duration d(t1 - t0);
        //    std::cout << "calculated 2D Heuristic in ms: " << d * 1000 << std::endl;
    }

    if (Constants::twoD)
    {
        // offset for same node in cell
        twoDoffset = sqrt(((start.getX() - (long)start.getX()) - (goal.getX() - (long)goal.getX())) *
                              ((start.getX() - (long)start.getX()) - (goal.getX() - (long)goal.getX())) +
                          ((start.getY() - (long)start.getY()) - (goal.getY() - (long)goal.getY())) *
                              ((start.getY() - (long)start.getY()) - (goal.getY() - (long)goal.getY())));
        twoDCost   = nodes2D[(int)start.getY() * width + (int)start.getX()].getG() - twoDoffset;
    }

    // return the maximum of the heuristics, making the heuristic admissable
    start.setH(std::max(reedsSheppCost, std::max(dubinsCost, twoDCost)));
}

//###################################################
//                                        DUBINS SHOT
//###################################################
Node3D* dubinsShot(Node3D& start, const Node3D& goal, CollisionDetection& configurationSpace)
{
    // start
    double q0[] = {start.getX(), start.getY(), start.getT()};
    // goal
    double q1[] = {goal.getX(), goal.getY(), goal.getT()};
    // initialize the path
    DubinsPath path;
    // calculate the path
    dubins_init(q0, q1, Constants::r, &path);

    int   i      = 0;
    float x      = 0.f;
    float length = dubins_path_length(&path);

    Node3D* dubinsNodes = new Node3D[(int)(length / Constants::dubinsStepSize) + 1];

    while (x < length)
    {
        double q[3];
        dubins_path_sample(&path, x, q);
        dubinsNodes[i].setX(q[0]);
        dubinsNodes[i].setY(q[1]);
        dubinsNodes[i].setT(Helper::normalizeHeadingRad(q[2]));

        // collision check
        if (configurationSpace.isTraversable(&dubinsNodes[i]))
        {
            // set the predecessor to the previous step
            if (i > 0)
            {
                dubinsNodes[i].setPred(&dubinsNodes[i - 1]);
            }
            else
            {
                dubinsNodes[i].setPred(&start);
            }

            if (&dubinsNodes[i] == dubinsNodes[i].getPred())
            {
                std::cout << "looping shot";
            }

            x += Constants::dubinsStepSize;
            i++;
        }
        else
        {
            //      std::cout << "Dubins shot collided, discarding the path" << "\n";
            // delete all nodes
            delete[] dubinsNodes;
            return nullptr;
        }
    }

    //  std::cout << "Dubins shot connected, returning the path" << "\n";
    return &dubinsNodes[i - 1];
}
