// The canvas for rendering with the 2d API
//
// Copyright (C) 2012  Thomas Geymayer <tomgey@gmail.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA

#include "Canvas.hxx"
#include <simgear/canvas/MouseEvent.hxx>
#include <simgear/scene/util/parse_color.hxx>
#include <simgear/scene/util/RenderConstants.hxx>

#include <osg/Camera>
#include <osg/Geode>
#include <osgText/Text>
#include <osgViewer/Viewer>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/foreach.hpp>
#include <iostream>

namespace simgear
{
namespace canvas
{

  //----------------------------------------------------------------------------
  Canvas::CullCallback::CullCallback(const CanvasWeakPtr& canvas):
    _canvas( canvas )
  {

  }

  //----------------------------------------------------------------------------
  void Canvas::CullCallback::operator()( osg::Node* node,
                                         osg::NodeVisitor* nv )
  {
    if( (nv->getTraversalMask() & simgear::MODEL_BIT) && !_canvas.expired() )
      _canvas.lock()->enableRendering();

    traverse(node, nv);
  }

  //----------------------------------------------------------------------------
  Canvas::Canvas(SGPropertyNode* node):
    PropertyBasedElement(node),
    _canvas_mgr(0),
    _size_x(-1),
    _size_y(-1),
    _view_width(-1),
    _view_height(-1),
    _status(node, "status"),
    _status_msg(node, "status-msg"),
    _mouse_x(node, "mouse/x"),
    _mouse_y(node, "mouse/y"),
    _mouse_dx(node, "mouse/dx"),
    _mouse_dy(node, "mouse/dy"),
    _mouse_button(node, "mouse/button"),
    _mouse_state(node, "mouse/state"),
    _mouse_mod(node, "mouse/mod"),
    _mouse_scroll(node, "mouse/scroll"),
    _mouse_event(node, "mouse/event"),
    _sampling_dirty(false),
    _render_dirty(true),
    _visible(true),
    _render_always(false)
  {
    _status = 0;
    setStatusFlags(MISSING_SIZE_X | MISSING_SIZE_Y);
  }

  //----------------------------------------------------------------------------
  Canvas::~Canvas()
  {

  }

  //----------------------------------------------------------------------------
  void Canvas::setSystemAdapter(const SystemAdapterPtr& system_adapter)
  {
    _system_adapter = system_adapter;
    _texture.setSystemAdapter(system_adapter);
  }

  //----------------------------------------------------------------------------
  SystemAdapterPtr Canvas::getSystemAdapter() const
  {
    return _system_adapter;
  }

  //----------------------------------------------------------------------------
  void Canvas::setCanvasMgr(CanvasMgr* canvas_mgr)
  {
    _canvas_mgr = canvas_mgr;
  }

  //----------------------------------------------------------------------------
  CanvasMgr* Canvas::getCanvasMgr() const
  {
    return _canvas_mgr;
  }

  //----------------------------------------------------------------------------
  void Canvas::addDependentCanvas(const CanvasWeakPtr& canvas)
  {
    if( canvas.expired() )
    {
      SG_LOG
      (
        SG_GENERAL,
        SG_WARN,
        "Canvas::addDependentCanvas: got an expired Canvas dependent on "
        << _node->getPath()
      );
      return;
    }

    _dependent_canvases.insert(canvas);
  }

  //----------------------------------------------------------------------------
  void Canvas::removeDependentCanvas(const CanvasWeakPtr& canvas)
  {
    _dependent_canvases.erase(canvas);
  }

  //----------------------------------------------------------------------------
  GroupPtr Canvas::createGroup(const std::string& name)
  {
    return boost::dynamic_pointer_cast<Group>
    (
      _root_group->createChild("group", name)
    );
  }

  //----------------------------------------------------------------------------
  void Canvas::enableRendering(bool force)
  {
    _visible = true;
    if( force )
      _render_dirty = true;
  }

  //----------------------------------------------------------------------------
  void Canvas::update(double delta_time_sec)
  {
    if( !_texture.serviceable() )
    {
      if( _status != STATUS_OK )
        return;

      _texture.setSize(_size_x, _size_y);
      _texture.useImageCoords(true);
      _texture.useStencil(true);
      _texture.allocRT(/*_camera_callback*/);

      osg::Camera* camera = _texture.getCamera();

      osg::Vec4 clear_color(0.0f, 0.0f, 0.0f , 1.0f);
      parseColor(_node->getStringValue("background"), clear_color);
      camera->setClearColor(clear_color);

      camera->addChild(_root_group->getMatrixTransform());

      // Ensure objects are drawn in order of traversal
      camera->getOrCreateStateSet()->setBinName("TraversalOrderBin");

      if( _texture.serviceable() )
      {
        setStatusFlags(STATUS_OK);
      }
      else
      {
        setStatusFlags(CREATE_FAILED);
        return;
      }
    }

    if( _visible || _render_always )
    {
      if( _render_dirty )
      {
        // Also mark all dependent (eg. recursively used) canvases as dirty
        BOOST_FOREACH(CanvasWeakPtr canvas, _dependent_canvases)
        {
          if( !canvas.expired() )
            canvas.lock()->_render_dirty = true;
        }
      }

      _texture.setRender(_render_dirty);

      _render_dirty = false;
      _visible = false;
    }
    else
      _texture.setRender(false);

    _root_group->update(delta_time_sec);

    if( _sampling_dirty )
    {
      _texture.setSampling(
        _node->getBoolValue("mipmapping"),
        _node->getIntValue("coverage-samples"),
        _node->getIntValue("color-samples")
      );
      _sampling_dirty = false;
      _render_dirty = true;
    }

    while( !_dirty_placements.empty() )
    {
      SGPropertyNode *node = _dirty_placements.back();
      _dirty_placements.pop_back();

      if( node->getIndex() >= static_cast<int>(_placements.size()) )
        // New placement
        _placements.resize(node->getIndex() + 1);
      else
        // Remove possibly existing placements
        _placements[ node->getIndex() ].clear();

      // Get new placements
      PlacementFactoryMap::const_iterator placement_factory =
        _placement_factories.find( node->getStringValue("type", "object") );
      if( placement_factory != _placement_factories.end() )
      {
        Placements& placements = _placements[ node->getIndex() ] =
          placement_factory->second
          (
            node,
            boost::static_pointer_cast<Canvas>(_self.lock())
          );
        node->setStringValue
        (
          "status-msg",
          placements.empty() ? "No match" : "Ok"
        );
      }
      else
        node->setStringValue("status-msg", "Unknown placement type");
    }
  }

  //----------------------------------------------------------------------------
  void Canvas::setSizeX(int sx)
  {
    if( _size_x == sx )
      return;
    _size_x = sx;

    // TODO resize if texture already allocated

    if( _size_x <= 0 )
      setStatusFlags(MISSING_SIZE_X);
    else
      setStatusFlags(MISSING_SIZE_X, false);

    // reset flag to allow creation with new size
    setStatusFlags(CREATE_FAILED, false);
  }

  //----------------------------------------------------------------------------
  void Canvas::setSizeY(int sy)
  {
    if( _size_y == sy )
      return;
    _size_y = sy;

    // TODO resize if texture already allocated

    if( _size_y <= 0 )
      setStatusFlags(MISSING_SIZE_Y);
    else
      setStatusFlags(MISSING_SIZE_Y, false);

    // reset flag to allow creation with new size
    setStatusFlags(CREATE_FAILED, false);
  }

  //----------------------------------------------------------------------------
  int Canvas::getSizeX() const
  {
    return _size_x;
  }

  //----------------------------------------------------------------------------
  int Canvas::getSizeY() const
  {
    return _size_y;
  }

  //----------------------------------------------------------------------------
  void Canvas::setViewWidth(int w)
  {
    if( _view_width == w )
      return;
    _view_width = w;

    _texture.setViewSize(_view_width, _view_height);
  }

  //----------------------------------------------------------------------------
  void Canvas::setViewHeight(int h)
  {
    if( _view_height == h )
      return;
    _view_height = h;

    _texture.setViewSize(_view_width, _view_height);
  }

  //----------------------------------------------------------------------------
  bool Canvas::handleMouseEvent(const MouseEvent& event)
  {
    _mouse_x = event.x;
    _mouse_y = event.y;
    _mouse_dx = event.dx;
    _mouse_dy = event.dy;
    _mouse_button = event.button;
    _mouse_state = event.state;
    _mouse_mod = event.mod;
    _mouse_scroll = event.scroll;
    // Always set event type last because all listeners are attached to it
    _mouse_event = event.type;

    if( _root_group.get() )
      return _root_group->handleMouseEvent(event);
    else
      return false;
  }

  //----------------------------------------------------------------------------
  void Canvas::childAdded( SGPropertyNode * parent,
                           SGPropertyNode * child )
  {
    if( parent != _node )
      return;

    if( child->getNameString() == "placement" )
      _dirty_placements.push_back(child);
    else if( _root_group.get() )
      static_cast<Element*>(_root_group.get())->childAdded(parent, child);
  }

  //----------------------------------------------------------------------------
  void Canvas::childRemoved( SGPropertyNode * parent,
                             SGPropertyNode * child )
  {
    _render_dirty = true;

    if( parent != _node )
      return;

    if( child->getNameString() == "placement" )
      _placements[ child->getIndex() ].clear();
    else if( _root_group.get() )
      static_cast<Element*>(_root_group.get())->childRemoved(parent, child);
  }

  //----------------------------------------------------------------------------
  void Canvas::valueChanged(SGPropertyNode* node)
  {
    if(    boost::starts_with(node->getNameString(), "status")
        || node->getParent()->getNameString() == "bounding-box" )
      return;
    _render_dirty = true;

    bool handled = true;
    if(    node->getParent()->getParent() == _node
        && node->getParent()->getNameString() == "placement" )
    {
      // prevent double updates...
      for( size_t i = 0; i < _dirty_placements.size(); ++i )
      {
        if( node->getParent() == _dirty_placements[i] )
          return;
      }

      _dirty_placements.push_back(node->getParent());
    }
    else if( node->getParent() == _node )
    {
      if( node->getNameString() == "background" )
      {
        osg::Vec4 color;
        if( _texture.getCamera() && parseColor(node->getStringValue(), color) )
        {
          _texture.getCamera()->setClearColor(color);
          _render_dirty = true;
        }
      }
      else if(    node->getNameString() == "mipmapping"
              || node->getNameString() == "coverage-samples"
              || node->getNameString() == "color-samples" )
      {
        _sampling_dirty = true;
      }
      else if( node->getNameString() == "render-always" )
      {
        _render_always = node->getBoolValue();
      }
      else if( node->getNameString() == "size" )
      {
        if( node->getIndex() == 0 )
          setSizeX( node->getIntValue() );
        else if( node->getIndex() == 1 )
          setSizeY( node->getIntValue() );
      }
      else if( node->getNameString() == "view" )
      {
        if( node->getIndex() == 0 )
          setViewWidth( node->getIntValue() );
        else if( node->getIndex() == 1 )
          setViewHeight( node->getIntValue() );
      }
      else if( node->getNameString() == "freeze" )
        _texture.setRender( node->getBoolValue() );
      else
        handled = false;
    }
    else
      handled = false;

    if( !handled && _root_group.get() )
      _root_group->valueChanged(node);
  }

  //----------------------------------------------------------------------------
  osg::Texture2D* Canvas::getTexture() const
  {
    return _texture.getTexture();
  }

  //----------------------------------------------------------------------------
  Canvas::CullCallbackPtr Canvas::getCullCallback() const
  {
    return _cull_callback;
  }

  //----------------------------------------------------------------------------
  void Canvas::addPlacementFactory( const std::string& type,
                                    PlacementFactory factory )
  {
    if( _placement_factories.find(type) != _placement_factories.end() )
      SG_LOG
      (
        SG_GENERAL,
        SG_WARN,
        "Canvas::addPlacementFactory: replace existing factor for type " << type
      );

    _placement_factories[type] = factory;
  }

  //----------------------------------------------------------------------------
  void Canvas::setSelf(const PropertyBasedElementPtr& self)
  {
    PropertyBasedElement::setSelf(self);

    CanvasPtr canvas = boost::static_pointer_cast<Canvas>(self);

    _root_group.reset( new Group(canvas, _node) );

    // Remove automatically created property listener as we forward them on our
    // own
    _root_group->removeListener();

    _cull_callback = new CullCallback(canvas);
  }

  //----------------------------------------------------------------------------
  void Canvas::setStatusFlags(unsigned int flags, bool set)
  {
    if( set )
      _status = _status | flags;
    else
      _status = _status & ~flags;
    // TODO maybe extend simgear::PropertyObject to allow |=, &= etc.

    if( (_status & MISSING_SIZE_X) && (_status & MISSING_SIZE_Y) )
      _status_msg = "Missing size";
    else if( _status & MISSING_SIZE_X )
      _status_msg = "Missing size-x";
    else if( _status & MISSING_SIZE_Y )
      _status_msg = "Missing size-y";
    else if( _status & CREATE_FAILED )
      _status_msg = "Creating render target failed";
    else if( _status == STATUS_OK && !_texture.serviceable() )
      _status_msg = "Creation pending...";
    else
      _status_msg = "Ok";
  }

  //----------------------------------------------------------------------------
  Canvas::PlacementFactoryMap Canvas::_placement_factories;

} // namespace canvas
} // namespace simgear